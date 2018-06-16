/*  			DirectSound
 *
 * Copyright 1998 Marcus Meissner
 * Copyright 1998 Rob Riggs
 * Copyright 2000-2001 TransGaming Technologies, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* Linux does not support better timing than 10ms */
#define DS_TIME_RES 2  /* Resolution of multimedia timer */
#define DS_TIME_DEL 10  /* Delay of multimedia timer callback, and duration of HEL fragment */
/* Default refresh count, can be overridden */
#define FAKE_REFRESH_COUNT (1000/DS_TIME_DEL/2)


#include <stdio.h>
#include <dsound.h>

#include "alc.h"
#include "al.h"
#include "alext.h"

#include "eax.h"

#ifndef AL_SOFT_map_buffer
#define AL_SOFT_map_buffer 1
typedef unsigned int ALbitfieldSOFT;
#define AL_MAP_READ_BIT_SOFT                     0x00000001
#define AL_MAP_WRITE_BIT_SOFT                    0x00000002
#define AL_MAP_PERSISTENT_BIT_SOFT               0x00000004
#define AL_PRESERVE_DATA_BIT_SOFT                0x00000008
typedef void (AL_APIENTRY*LPALBUFFERSTORAGESOFT)(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags);
typedef void* (AL_APIENTRY*LPALMAPBUFFERSOFT)(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access);
typedef void (AL_APIENTRY*LPALUNMAPBUFFERSOFT)(ALuint buffer);
typedef void (AL_APIENTRY*LPALFLUSHMAPPEDBUFFERSOFT)(ALuint buffer, ALsizei offset, ALsizei length);
#endif


extern int LogLevel;

#define DO_PRINT(a, ...) fprintf(stderr, a, __VA_ARGS__)
#ifdef _MSC_VER
#define TRACE(fmt, ...) do {                                              \
    if(LogLevel >= 3)                                                     \
        DO_PRINT("%04x:trace:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, __VA_ARGS__);                              \
} while(0)
#define WARN(fmt, ...) do {                                              \
    if(LogLevel >= 2)                                                    \
        DO_PRINT("%04x:warn:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, __VA_ARGS__);                             \
} while(0)
#define FIXME(fmt, ...) do {                                              \
    if(LogLevel >= 1)                                                     \
        DO_PRINT("%04x:fixme:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, __VA_ARGS__);                              \
} while(0)
#define ERR(fmt, ...) do {                                              \
    if(LogLevel >= 0)                                                   \
        DO_PRINT("%04x:err:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, __VA_ARGS__);                            \
} while(0)

#else

#define TRACE(fmt, ...) do {                                              \
    if(LogLevel >= 3)                                                     \
        DO_PRINT("%04x:trace:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, ## __VA_ARGS__);                           \
} while(0)
#define WARN(fmt, ...) do {                                              \
    if(LogLevel >= 2)                                                    \
        DO_PRINT("%04x:warn:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, ## __VA_ARGS__);                          \
} while(0)
#define FIXME(fmt, ...) do {                                              \
    if(LogLevel >= 1)                                                     \
        DO_PRINT("%04x:fixme:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, ## __VA_ARGS__);                           \
} while(0)
#define ERR(fmt, ...) do {                                              \
    if(LogLevel >= 0)                                                   \
        DO_PRINT("%04x:err:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, ## __VA_ARGS__);                         \
} while(0)
#endif

const char *wine_dbg_sprintf( const char *format, ... );
const char *wine_dbgstr_wn( const WCHAR *str, int n );
const char *debugstr_guid( const GUID *id );

static inline const char *debugstr_w( const WCHAR *s ) { return wine_dbgstr_wn( s, -1 ); }


#ifndef U64
#if defined(_MSC_VER)
#define U64(x) (x##ui64)
#elif SIZEOF_LONG == 8
#define U64(x) (x##ul)
#else
#define U64(x) (x##ull)
#endif
#endif

/* Define a CTZ64 macro (count trailing zeros, for 64-bit integers). The result
 * is *UNDEFINED* if the value is 0.
 */
#ifdef __GNUC__

#if SIZEOF_LONG == 8
#define CTZ64 __builtin_ctzl
#else
#define CTZ64 __builtin_ctzll
#endif

#elif defined(HAVE_BITSCANFORWARD64_INTRINSIC)

static inline int msvc64_ctz64(DWORD64 v)
{
    unsigned long idx = 64;
    _BitScanForward64(&idx, v);
    return (int)idx;
}
#define CTZ64 msvc64_ctz64

#elif defined(HAVE_BITSCANFORWARD_INTRINSIC)

static inline int msvc_ctz64(DWORD64 v)
{
    unsigned long idx = 64;
    if(!_BitScanForward(&idx, v&0xffffffff))
    {
        if(_BitScanForward(&idx, v>>32))
            idx += 32;
    }
    return (int)idx;
}
#define CTZ64 msvc_ctz64

#else

/* There be black magics here. The popcnt64 method is derived from
 * https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
 * while the ctz-utilizing-popcnt algorithm is shown here
 * http://www.hackersdelight.org/hdcodetxt/ntz.c.txt
 * as the ntz2 variant. These likely aren't the most efficient methods, but
 * they're good enough if the GCC or MSVC intrinsics aren't available.
 */
static inline int fallback_popcnt64(DWORD64 v)
{
    v = v - ((v >> 1) & U64(0x5555555555555555));
    v = (v & U64(0x3333333333333333)) + ((v >> 2) & U64(0x3333333333333333));
    v = (v + (v >> 4)) & U64(0x0f0f0f0f0f0f0f0f);
    return (int)((v * U64(0x0101010101010101)) >> 56);
}

static inline int fallback_ctz64(DWORD64 value)
{
    return fallback_popcnt64(~value & (value - 1));
}
#define CTZ64 fallback_ctz64
#endif

#ifdef __GNUC__
#define LIKELY(x) __builtin_expect(!!(x), !0)
#define UNLIKELY(x) __builtin_expect(!!(x), !!0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif


/* All openal functions */
extern int openal_loaded;
extern LPALCCREATECONTEXT palcCreateContext;
extern LPALCMAKECONTEXTCURRENT palcMakeContextCurrent;
extern LPALCPROCESSCONTEXT palcProcessContext;
extern LPALCSUSPENDCONTEXT palcSuspendContext;
extern LPALCDESTROYCONTEXT palcDestroyContext;
extern LPALCGETCURRENTCONTEXT palcGetCurrentContext;
extern LPALCGETCONTEXTSDEVICE palcGetContextsDevice;
extern LPALCOPENDEVICE palcOpenDevice;
extern LPALCCLOSEDEVICE palcCloseDevice;
extern LPALCGETERROR palcGetError;
extern LPALCISEXTENSIONPRESENT palcIsExtensionPresent;
extern LPALCGETPROCADDRESS palcGetProcAddress;
extern LPALCGETENUMVALUE palcGetEnumValue;
extern LPALCGETSTRING palcGetString;
extern LPALCGETINTEGERV palcGetIntegerv;
extern LPALCCAPTUREOPENDEVICE palcCaptureOpenDevice;
extern LPALCCAPTURECLOSEDEVICE palcCaptureCloseDevice;
extern LPALCCAPTURESTART palcCaptureStart;
extern LPALCCAPTURESTOP palcCaptureStop;
extern LPALCCAPTURESAMPLES palcCaptureSamples;
extern LPALENABLE palEnable;
extern LPALDISABLE palDisable;
extern LPALISENABLED palIsEnabled;
extern LPALGETSTRING palGetString;
extern LPALGETBOOLEANV palGetBooleanv;
extern LPALGETINTEGERV palGetIntegerv;
extern LPALGETFLOATV palGetFloatv;
extern LPALGETDOUBLEV palGetDoublev;
extern LPALGETBOOLEAN palGetBoolean;
extern LPALGETINTEGER palGetInteger;
extern LPALGETFLOAT palGetFloat;
extern LPALGETDOUBLE palGetDouble;
extern LPALGETERROR palGetError;
extern LPALISEXTENSIONPRESENT palIsExtensionPresent;
extern LPALGETPROCADDRESS palGetProcAddress;
extern LPALGETENUMVALUE palGetEnumValue;
extern LPALLISTENERF palListenerf;
extern LPALLISTENER3F palListener3f;
extern LPALLISTENERFV palListenerfv;
extern LPALLISTENERI palListeneri;
extern LPALLISTENER3I palListener3i;
extern LPALLISTENERIV palListeneriv;
extern LPALGETLISTENERF palGetListenerf;
extern LPALGETLISTENER3F palGetListener3f;
extern LPALGETLISTENERFV palGetListenerfv;
extern LPALGETLISTENERI palGetListeneri;
extern LPALGETLISTENER3I palGetListener3i;
extern LPALGETLISTENERIV palGetListeneriv;
extern LPALGENSOURCES palGenSources;
extern LPALDELETESOURCES palDeleteSources;
extern LPALISSOURCE palIsSource;
extern LPALSOURCEF palSourcef;
extern LPALSOURCE3F palSource3f;
extern LPALSOURCEFV palSourcefv;
extern LPALSOURCEI palSourcei;
extern LPALSOURCE3I palSource3i;
extern LPALSOURCEIV palSourceiv;
extern LPALGETSOURCEF palGetSourcef;
extern LPALGETSOURCE3F palGetSource3f;
extern LPALGETSOURCEFV palGetSourcefv;
extern LPALGETSOURCEI palGetSourcei;
extern LPALGETSOURCE3I palGetSource3i;
extern LPALGETSOURCEIV palGetSourceiv;
extern LPALSOURCEPLAYV palSourcePlayv;
extern LPALSOURCESTOPV palSourceStopv;
extern LPALSOURCEREWINDV palSourceRewindv;
extern LPALSOURCEPAUSEV palSourcePausev;
extern LPALSOURCEPLAY palSourcePlay;
extern LPALSOURCESTOP palSourceStop;
extern LPALSOURCEREWIND palSourceRewind;
extern LPALSOURCEPAUSE palSourcePause;
extern LPALSOURCEQUEUEBUFFERS palSourceQueueBuffers;
extern LPALSOURCEUNQUEUEBUFFERS palSourceUnqueueBuffers;
extern LPALGENBUFFERS palGenBuffers;
extern LPALDELETEBUFFERS palDeleteBuffers;
extern LPALISBUFFER palIsBuffer;
extern LPALBUFFERF palBufferf;
extern LPALBUFFER3F palBuffer3f;
extern LPALBUFFERFV palBufferfv;
extern LPALBUFFERI palBufferi;
extern LPALBUFFER3I palBuffer3i;
extern LPALBUFFERIV palBufferiv;
extern LPALGETBUFFERF palGetBufferf;
extern LPALGETBUFFER3F palGetBuffer3f;
extern LPALGETBUFFERFV palGetBufferfv;
extern LPALGETBUFFERI palGetBufferi;
extern LPALGETBUFFER3I palGetBuffer3i;
extern LPALGETBUFFERIV palGetBufferiv;
extern LPALBUFFERDATA palBufferData;
extern LPALDOPPLERFACTOR palDopplerFactor;
extern LPALDOPPLERVELOCITY palDopplerVelocity;
extern LPALDISTANCEMODEL palDistanceModel;
extern LPALSPEEDOFSOUND palSpeedOfSound;

#define alcCreateContext palcCreateContext
#define alcMakeContextCurrent palcMakeContextCurrent
#define alcProcessContext palcProcessContext
#define alcSuspendContext palcSuspendContext
#define alcDestroyContext palcDestroyContext
#define alcGetCurrentContext palcGetCurrentContext
#define alcGetContextsDevice palcGetContextsDevice
#define alcOpenDevice palcOpenDevice
#define alcCloseDevice palcCloseDevice
#define alcGetError palcGetError
#define alcIsExtensionPresent palcIsExtensionPresent
#define alcGetProcAddress palcGetProcAddress
#define alcGetEnumValue palcGetEnumValue
#define alcGetString palcGetString
#define alcGetIntegerv palcGetIntegerv
#define alcCaptureOpenDevice palcCaptureOpenDevice
#define alcCaptureCloseDevice palcCaptureCloseDevice
#define alcCaptureStart palcCaptureStart
#define alcCaptureStop palcCaptureStop
#define alcCaptureSamples palcCaptureSamples
#define alEnable palEnable
#define alDisable palDisable
#define alIsEnabled palIsEnabled
#define alGetString palGetString
#define alGetBooleanv palGetBooleanv
#define alGetIntegerv palGetIntegerv
#define alGetFloatv palGetFloatv
#define alGetDoublev palGetDoublev
#define alGetBoolean palGetBoolean
#define alGetInteger palGetInteger
#define alGetFloat palGetFloat
#define alGetDouble palGetDouble
#define alGetError palGetError
#define alIsExtensionPresent palIsExtensionPresent
#define alGetProcAddress palGetProcAddress
#define alGetEnumValue palGetEnumValue
#define alListenerf palListenerf
#define alListener3f palListener3f
#define alListenerfv palListenerfv
#define alListeneri palListeneri
#define alListener3i palListener3i
#define alListeneriv palListeneriv
#define alGetListenerf palGetListenerf
#define alGetListener3f palGetListener3f
#define alGetListenerfv palGetListenerfv
#define alGetListeneri palGetListeneri
#define alGetListener3i palGetListener3i
#define alGetListeneriv palGetListeneriv
#define alGenSources palGenSources
#define alDeleteSources palDeleteSources
#define alIsSource palIsSource
#define alSourcef palSourcef
#define alSource3f palSource3f
#define alSourcefv palSourcefv
#define alSourcei palSourcei
#define alSource3i palSource3i
#define alSourceiv palSourceiv
#define alGetSourcef palGetSourcef
#define alGetSource3f palGetSource3f
#define alGetSourcefv palGetSourcefv
#define alGetSourcei palGetSourcei
#define alGetSource3i palGetSource3i
#define alGetSourceiv palGetSourceiv
#define alSourcePlayv palSourcePlayv
#define alSourceStopv palSourceStopv
#define alSourceRewindv palSourceRewindv
#define alSourcePausev palSourcePausev
#define alSourcePlay palSourcePlay
#define alSourceStop palSourceStop
#define alSourceRewind palSourceRewind
#define alSourcePause palSourcePause
#define alSourceQueueBuffers palSourceQueueBuffers
#define alSourceUnqueueBuffers palSourceUnqueueBuffers
#define alGenBuffers palGenBuffers
#define alDeleteBuffers palDeleteBuffers
#define alIsBuffer palIsBuffer
#define alBufferf palBufferf
#define alBuffer3f palBuffer3f
#define alBufferfv palBufferfv
#define alBufferi palBufferi
#define alBuffer3i palBuffer3i
#define alBufferiv palBufferiv
#define alGetBufferf palGetBufferf
#define alGetBuffer3f palGetBuffer3f
#define alGetBufferfv palGetBufferfv
#define alGetBufferi palGetBufferi
#define alGetBuffer3i palGetBuffer3i
#define alGetBufferiv palGetBufferiv
#define alBufferData palBufferData
#define alDopplerFactor palDopplerFactor
#define alDopplerVelocity palDopplerVelocity
#define alDistanceModel palDistanceModel
#define alSpeedOfSound palSpeedOfSound

#include <math.h>
#include "wingdi.h"
#include "mmreg.h"

/* OpenAL only allows for 1 single access to the device at the same time */
extern CRITICAL_SECTION openal_crst;

extern LPALCMAKECONTEXTCURRENT set_context;
extern LPALCGETCURRENTCONTEXT get_context;
extern BOOL local_contexts;


extern DWORD TlsThreadPtr;
extern void (*EnterALSection)(ALCcontext *ctx);
extern void (*LeaveALSection)(void);


typedef struct DS8Impl DS8Impl;
typedef struct DS8Primary DS8Primary;
typedef struct DS8Buffer DS8Buffer;


enum {
    EXT_EFX,
    EXT_FLOAT32,
    EXT_MCFORMATS,
    SOFT_DEFERRED_UPDATES,
    SOFTX_MAP_BUFFER,

    MAX_EXTENSIONS
};

typedef struct ExtALFuncs {
    LPALGENEFFECTS GenEffects;
    LPALDELETEEFFECTS DeleteEffects;
    LPALEFFECTI Effecti;
    LPALEFFECTF Effectf;

    LPALGENAUXILIARYEFFECTSLOTS GenAuxiliaryEffectSlots;
    LPALDELETEAUXILIARYEFFECTSLOTS DeleteAuxiliaryEffectSlots;
    LPALAUXILIARYEFFECTSLOTI AuxiliaryEffectSloti;

    LPALDEFERUPDATESSOFT DeferUpdatesSOFT;
    LPALPROCESSUPDATESSOFT ProcessUpdatesSOFT;

    LPALBUFFERSTORAGESOFT BufferStorageSOFT;
    LPALMAPBUFFERSOFT MapBufferSOFT;
    LPALUNMAPBUFFERSOFT UnmapBufferSOFT;
    LPALFLUSHMAPPEDBUFFERSOFT FlushMappedBufferSOFT;
} ExtALFuncs;

#define MAX_SOURCES 256
typedef struct DeviceShare {
    LONG ref;

    ALCdevice *device;
    ALCcontext *ctx;
    ALCint refresh;

    ALboolean SupportedExt[MAX_EXTENSIONS];

    ExtALFuncs ExtAL;

    CRITICAL_SECTION crst;

    ALuint sources[MAX_SOURCES];
    DWORD nsources, max_sources;

    ALuint auxslot;

    HANDLE thread_hdl;
    DWORD thread_id;

    HANDLE queue_timer;
    HANDLE timer_evt;
    volatile LONG quit_now;

    ALsizei nprimaries;
    DS8Primary **primaries;

    GUID guid;
} DeviceShare;


typedef struct DS8Data {
    LONG ref;

    DS8Primary *primary;

    /* Lock was called and unlock isn't? */
    LONG locked;

    WAVEFORMATEXTENSIBLE format;

    ALsizei buf_size;
    ALenum buf_format;
    DWORD dsbflags;
    BYTE *data;
    ALuint bid;
} DS8Data;
/* Amount of buffers that have to be queued when
 * bufferdatastatic and buffersubdata are not available */
#define QBUFFERS 4

union BufferParamFlags {
    LONG flags;
    struct {
        BOOL pos : 1;
        BOOL vel : 1;
        BOOL cone_angles : 1;
        BOOL cone_orient : 1;
        BOOL cone_outsidevolume : 1;
        BOOL min_distance : 1;
        BOOL max_distance : 1;
        BOOL mode : 1;
    } bit;
};

struct DS8Buffer {
    IDirectSoundBuffer8 IDirectSoundBuffer8_iface;
    IDirectSoundBuffer IDirectSoundBuffer_iface;
    IDirectSound3DBuffer IDirectSound3DBuffer_iface;
    IDirectSoundNotify IDirectSoundNotify_iface;
    IKsPropertySet IKsPropertySet_iface;

    LONG ref, ds3d_ref, not_ref, prop_ref;
    LONG all_ref;

    DS8Primary *primary;

    /* From the primary */
    ALCcontext *ctx;
    const ExtALFuncs *ExtAL;
    CRITICAL_SECTION *crst;

    DS8Data *buffer;
    ALuint source;

    ALsizei segsize;
    ALsizei data_offset;
    ALsizei queue_base;
    ALsizei curidx;
    ALuint stream_bids[QBUFFERS];

    DWORD isplaying : 1;
    DWORD islooping : 1;
    DWORD bufferlost : 1;
    DWORD playflags : 29;
    DWORD ds3dmode;

    DS3DBUFFER params;
    union BufferParamFlags dirty;

    DWORD nnotify, lastpos;
    DSBPOSITIONNOTIFY *notify;
};


struct DSBufferGroup {
    DWORD64 FreeBuffers;
    DS8Buffer Buffers[64];
};


union PrimaryParamFlags {
    LONG flags;
    struct {
        BOOL pos : 1;
        BOOL vel : 1;
        BOOL orientation : 1;
        BOOL distancefactor : 1;
        BOOL rollofffactor : 1;
        BOOL dopplerfactor : 1;
        BOOL effect : 1;
    } bit;
};

struct DS8Primary {
    IDirectSoundBuffer IDirectSoundBuffer_iface;
    IDirectSound3DListener IDirectSound3DListener_iface;
    IKsPropertySet IKsPropertySet_iface;

    LONG ref, ds3d_ref, prop_ref;
    IDirectSoundBuffer8 *write_emu;
    DS8Impl *parent;

    /* Taken from the share */
    ALCcontext *ctx;
    const ALboolean *SupportedExt;
    const ExtALFuncs *ExtAL;
    CRITICAL_SECTION *crst;
    ALCint refresh;
    ALuint *sources;
    ALuint auxslot;

    DWORD buf_size;
    BOOL stopped;
    DWORD flags;
    WAVEFORMATEXTENSIBLE format;

    LPALDEFERUPDATESSOFT DeferUpdates;
    LPALPROCESSUPDATESSOFT ProcessUpdates;

    DS8Buffer **notifies;
    DWORD nnotifies, sizenotifies;

    ALuint effect;
    ALfloat rollofffactor;

    DS3DLISTENER params;
    union PrimaryParamFlags dirty;

    EAXLISTENERPROPERTIES eax_prop;

    DWORD NumBufferGroups;
    struct DSBufferGroup *BufferGroups;
};


/* Device implementation */
struct DS8Impl {
    IDirectSound8 IDirectSound8_iface;
    IDirectSound IDirectSound_iface;
    IUnknown IUnknown_iface;

    LONG ref, unkref, dsref;
    BOOL is_8;

    DeviceShare *share;

    /* Taken from the share */
    ALCdevice *device;

    DS8Primary primary;

    DWORD speaker_config;
    DWORD prio_level;
};


const ALCchar *DSOUND_getdevicestrings(void);
const ALCchar *DSOUND_getcapturedevicestrings(void);

HRESULT DS8Primary_PreInit(DS8Primary *prim, DS8Impl *parent);
void DS8Primary_Clear(DS8Primary *prim);
void DS8Primary_triggernots(DS8Primary *prim);
void DS8Primary_streamfeeder(DS8Primary *prim, BYTE *scratch_mem/*2K non-permanent memory*/);
HRESULT WINAPI DS8Primary3D_CommitDeferredSettings(IDirectSound3DListener *iface);

HRESULT DS8Buffer_Create(DS8Buffer **ppv, DS8Primary *parent, IDirectSoundBuffer *orig);
void DS8Buffer_Destroy(DS8Buffer *buf);
void DS8Buffer_SetParams(DS8Buffer *buffer, const DS3DBUFFER *params, LONG flags);
HRESULT WINAPI DS8Buffer_GetCurrentPosition(IDirectSoundBuffer8 *iface, DWORD *playpos, DWORD *curpos);
HRESULT WINAPI DS8Buffer_GetStatus(IDirectSoundBuffer8 *iface, DWORD *status);

static inline LONG gain_to_mB(float gain)
{
    return (LONG)(log10f(gain) * 2000.0f);
}
static inline float mB_to_gain(LONG millibels)
{
    return powf(10.0f, (float)millibels/2000.0f);
}
static inline float mBF_to_gain(float millibels)
{
    return powf(10.0f, millibels/2000.0f);
}

static inline LONG clampI(LONG val, LONG minval, LONG maxval)
{
    if(val >= maxval) return maxval;
    if(val <= minval) return minval;
    return val;
}
static inline ULONG clampU(ULONG val, ULONG minval, ULONG maxval)
{
    if(val >= maxval) return maxval;
    if(val <= minval) return minval;
    return val;
}
static inline FLOAT clampF(FLOAT val, FLOAT minval, FLOAT maxval)
{
    if(val >= maxval) return maxval;
    if(val <= minval) return minval;
    return val;
}


#define checkALError() do {                                                   \
    ALenum err = alGetError();                                                \
    if(err != AL_NO_ERROR)                                                    \
        ERR(">>>>>>>>>>>> Received AL error %#x on context %p, %s:%u\n",      \
            err, get_context(), __FUNCTION__, __LINE__);                      \
} while (0)

#define checkALCError(dev) do {                                               \
    ALenum err = alcGetError(dev);                                            \
    if(err != ALC_NO_ERROR)                                                   \
        ERR(">>>>>>>>>>>> Received ALC error %#x on device %p, %s:%u\n",      \
            err, dev, __FUNCTION__, __LINE__);                                \
} while(0)


#define setALContext(actx) EnterALSection(actx)
#define popALContext() LeaveALSection()


HRESULT DSOUND_Create(REFIID riid, void **ppDS);
HRESULT DSOUND_Create8(REFIID riid, void **ppDS);
HRESULT DSOUND_FullDuplexCreate(REFIID riid, void **ppDSFD);
HRESULT IKsPrivatePropertySetImpl_Create(REFIID riid, void **piks);
HRESULT DSOUND_CaptureCreate(REFIID riid, void **ppDSC);
HRESULT DSOUND_CaptureCreate8(REFIID riid, void **ppDSC);

extern const GUID DSOUND_renderer_guid;
extern const GUID DSOUND_capture_guid;
