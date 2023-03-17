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
#include <math.h>
#include <dsound.h>
#include <mmdeviceapi.h>
#include <devpropdef.h>

#include "wingdi.h"
#include "mmreg.h"

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


#ifdef __GNUC__
#define LIKELY(x) __builtin_expect(!!(x), !0)
#define UNLIKELY(x) __builtin_expect(!!(x), !!0)
#else
#define LIKELY(x) (!!(x))
#define UNLIKELY(x) (!!(x))
#endif


extern int LogLevel;
extern FILE *LogFile;

#define DO_PRINT(a, ...) do {         \
    fprintf(LogFile, a, __VA_ARGS__); \
    fflush(LogFile);                  \
} while(0)

#ifdef _MSC_VER
#define TRACE(fmt, ...) do {                                              \
    if(UNLIKELY(LogLevel >= 3))                                           \
        DO_PRINT("%04x:trace:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, __VA_ARGS__);                              \
} while(0)
#define WARN(fmt, ...) do {                                              \
    if(UNLIKELY(LogLevel >= 2))                                          \
        DO_PRINT("%04x:warn:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, __VA_ARGS__);                             \
} while(0)
#define FIXME(fmt, ...) do {                                              \
    if(UNLIKELY(LogLevel >= 1))                                           \
        DO_PRINT("%04x:fixme:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, __VA_ARGS__);                              \
} while(0)
#define ERR(fmt, ...) do {                                              \
    if(UNLIKELY(LogLevel >= 0))                                         \
        DO_PRINT("%04x:err:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, __VA_ARGS__);                            \
} while(0)

#else

#define TRACE(fmt, ...) do {                                              \
    if(UNLIKELY(LogLevel >= 3))                                           \
        DO_PRINT("%04x:trace:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, ## __VA_ARGS__);                           \
} while(0)
#define WARN(fmt, ...) do {                                              \
    if(UNLIKELY(LogLevel >= 2))                                          \
        DO_PRINT("%04x:warn:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, ## __VA_ARGS__);                          \
} while(0)
#define FIXME(fmt, ...) do {                                              \
    if(UNLIKELY(LogLevel >= 1))                                           \
        DO_PRINT("%04x:fixme:dsound:%s " fmt, (UINT)GetCurrentThreadId(), \
                 __FUNCTION__, ## __VA_ARGS__);                           \
} while(0)
#define ERR(fmt, ...) do {                                              \
    if(UNLIKELY(LogLevel >= 0))                                         \
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
#define POPCNT64 __builtin_popcountl
#define CTZ64 __builtin_ctzl
#else
#define POPCNT64 __builtin_popcountll
#define CTZ64 __builtin_ctzll
#endif

#else

/* There be black magics here. The popcnt64 method is derived from
 * https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
 */
static inline int fallback_popcnt64(DWORD64 v)
{
    v = v - ((v >> 1) & U64(0x5555555555555555));
    v = (v & U64(0x3333333333333333)) + ((v >> 2) & U64(0x3333333333333333));
    v = (v + (v >> 4)) & U64(0x0f0f0f0f0f0f0f0f);
    return (int)((v * U64(0x0101010101010101)) >> 56);
}
#define POPCNT64 fallback_popcnt64

#if defined(HAVE_BITSCANFORWARD64_INTRINSIC)

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

static inline int fallback_ctz64(DWORD64 value)
{
    return fallback_popcnt64(~value & (value - 1));
}
#define CTZ64 fallback_ctz64
#endif
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

/* Extension functions. Technically device- or driver-specific, but as long as
 * they're pulled from the NULL device it should be routed correctly.
 */
typedef ALenum(AL_APIENTRY*LPEAXSET)(
    const GUID* property_set_id,
    ALuint property_id,
    ALuint property_source_id,
    ALvoid* property_buffer,
    ALuint property_size);
typedef ALenum(AL_APIENTRY*LPEAXGET)(
    const GUID* property_set_id,
    ALuint property_id,
    ALuint property_source_id,
    ALvoid* property_buffer,
    ALuint property_size);

extern LPEAXSET pEAXSet;
extern LPEAXGET pEAXGet;
extern LPALDEFERUPDATESSOFT palDeferUpdatesSOFT;
extern LPALPROCESSUPDATESSOFT palProcessUpdatesSOFT;
extern LPALBUFFERSTORAGESOFT palBufferStorageSOFT;
extern LPALMAPBUFFERSOFT palMapBufferSOFT;
extern LPALUNMAPBUFFERSOFT palUnmapBufferSOFT;
extern LPALFLUSHMAPPEDBUFFERSOFT palFlushMappedBufferSOFT;

#define EAXSet pEAXSet
#define EAXGet pEAXGet
#define alDeferUpdatesSOFT palDeferUpdatesSOFT
#define alProcessUpdatesSOFT palProcessUpdatesSOFT
#define alBufferStorageSOFT palBufferStorageSOFT
#define alMapBufferSOFT palMapBufferSOFT
#define alUnmapBufferSOFT palUnmapBufferSOFT
#define alFlushMappedBufferSOFT palFlushMappedBufferSOFT


#ifndef E_PROP_ID_UNSUPPORTED
#define E_PROP_ID_UNSUPPORTED           ((HRESULT)0x80070490)
#endif

typedef enum {
    DSPROPERTY_VMANAGER_MODE = 0,
    DSPROPERTY_VMANAGER_PRIORITY,
    DSPROPERTY_VMANAGER_STATE
} DSPROPERTY_VMANAGER;


typedef enum {
    DSPROPERTY_VMANAGER_MODE_DEFAULT = 0,
    DSPROPERTY_VMANAGER_MODE_AUTO,
    DSPROPERTY_VMANAGER_MODE_REPORT,
    DSPROPERTY_VMANAGER_MODE_USER,
    VMANAGER_MODE_MAX
} VmMode;


typedef enum {
    DSPROPERTY_VMANAGER_STATE_PLAYING3DHW = 0,
    DSPROPERTY_VMANAGER_STATE_SILENT,
    DSPROPERTY_VMANAGER_STATE_BUMPED,
    DSPROPERTY_VMANAGER_STATE_PLAYFAILED,
    VMANAGER_STATE_MAX
} VmState;

/* OpenAL only allows for 1 single access to the device at the same time */
extern CRITICAL_SECTION openal_crst;

extern LPALCMAKECONTEXTCURRENT set_context;
extern LPALCGETCURRENTCONTEXT get_context;
extern BOOL local_contexts;


extern DWORD TlsThreadPtr;
extern void (*EnterALSection)(ALCcontext *ctx);
extern void (*LeaveALSection)(void);


typedef struct DSDevice DSDevice;
typedef struct DSPrimary DSPrimary;
typedef struct DSBuffer DSBuffer;


enum {
    EXT_EAX,
    EXT_FLOAT32,
    EXT_MCFORMATS,
    SOFT_DEFERRED_UPDATES,
    SOFT_SOURCE_SPATIALIZE,
    SOFTX_MAP_BUFFER,

    MAX_EXTENSIONS
};


#define BITFIELD_ARRAY_SIZE(b) ((b+7) / 8)
#define BITFIELD_SET(arr, b) ((arr)[(b)>>3] |= 1<<((b)&7))
#define BITFIELD_TEST(arr, b) ((arr)[(b)>>3] & (1<<((b)&7)))

/* Maximum number of emulated hardware buffers. May be less depending on source
 * availability.
 */
#define MAX_HWBUFFERS 128

#define MAX_SOURCES 1024
typedef struct SourceCollection {
    DWORD maxhw_alloc, availhw_num;
    DWORD maxsw_alloc, availsw_num;
} SourceCollection;

typedef struct DeviceShare {
    LONG ref;

    ALCdevice *device;
    ALCcontext *ctx;
    ALCint refresh;

    ALboolean Exts[BITFIELD_ARRAY_SIZE(MAX_EXTENSIONS)];

    CRITICAL_SECTION crst;

    SourceCollection sources;

    HANDLE thread_hdl;
    DWORD thread_id;

    HANDLE queue_timer;
    HANDLE timer_evt;
    volatile LONG quit_now;

    ALsizei nprimaries;
    DSPrimary **primaries;

    GUID guid;
    DWORD speaker_config;
    
    DWORD vm_managermode;
} DeviceShare;

#define HAS_EXTENSION(s, e) BITFIELD_TEST((s)->Exts, e)


typedef struct DSData {
    LONG ref;

    DSPrimary *primary;

    /* Lock was called and unlock isn't? */
    LONG locked;

    WAVEFORMATEXTENSIBLE format;

    ALsizei buf_size;
    ALenum buf_format;
    DWORD dsbflags;
    BYTE *data;
    ALuint bid;
} DSData;
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

struct DSBuffer {
    IDirectSoundBuffer8 IDirectSoundBuffer8_iface;
    IDirectSound3DBuffer IDirectSound3DBuffer_iface;
    IDirectSoundNotify IDirectSoundNotify_iface;
    IKsPropertySet IKsPropertySet_iface;

    LONG ref, ds3d_ref, not_ref, prop_ref;
    LONG all_ref;

    DeviceShare *share;
    DSPrimary *primary;

    /* From the primary */
    ALCcontext *ctx;

    DSData *buffer;
    ALuint source;

    ALsizei segsize;
    ALsizei data_offset;
    ALsizei queue_base;
    ALsizei curidx;
    ALuint stream_bids[QBUFFERS];

    BOOL init_done : 1;
    BOOL isplaying : 1;
    BOOL islooping : 1;
    BOOL bufferlost : 1;

    /* Must be 0 (deferred, not yet placed), DSBSTATUS_LOCSOFTWARE, or
     * DSBSTATUS_LOCHARDWARE.
     */
    DWORD loc_status;

    struct {
        LONG vol, pan;
        DWORD frequency;
        DS3DBUFFER ds3d;
    } current;
    struct {
        DS3DBUFFER ds3d;
    } deferred;
    union BufferParamFlags dirty;

    DWORD nnotify, lastpos;
    DSBPOSITIONNOTIFY *notify;

    DWORD vm_voicepriority;
    //DWORD vm_voicestate;
};


struct DSBufferGroup {
    DWORD64 FreeBuffers;
    DSBuffer *Buffers;
};


enum {
    FXSLOT_EFFECT_REVERB,
    FXSLOT_EFFECT_CHORUS,

    FXSLOT_EFFECT_NULL,
};

union PrimaryParamFlags {
    LONG flags;
    struct {
        LONG pos : 1;
        LONG vel : 1;
        LONG orientation : 1;
        LONG distancefactor : 1;
        LONG rollofffactor : 1;
        LONG dopplerfactor : 1;
    } bit;
};

struct DSPrimary {
    IDirectSoundBuffer IDirectSoundBuffer_iface;
    IDirectSound3DListener IDirectSound3DListener_iface;
    IKsPropertySet IKsPropertySet_iface;

    LONG ref, ds3d_ref, prop_ref;
    IDirectSoundBuffer *write_emu;
    DSDevice *parent;

    DeviceShare *share;
    /* Taken from the share */
    ALCcontext *ctx;
    ALCint refresh;

    DWORD buf_size;
    BOOL stopped;
    DWORD flags;
    WAVEFORMATEXTENSIBLE format;

    DSBuffer **notifies;
    DWORD nnotifies, sizenotifies;

    ALint primary_idx;

    struct {
        DS3DLISTENER ds3d;
    } current;
    struct {
        DS3DLISTENER ds3d;
    } deferred;
    union PrimaryParamFlags dirty;

    DWORD NumBufferGroups;
    struct DSBufferGroup *BufferGroups;
};


/* Device implementation */
struct DSDevice {
    IDirectSound8 IDirectSound8_iface;
    IDirectSound IDirectSound_iface;
    IUnknown IUnknown_iface;

    LONG ref, unkref, dsref;
    BOOL is_8;

    DeviceShare *share;

    /* Taken from the share */
    ALCdevice *device;

    DSPrimary primary;

    DWORD prio_level;
};


DEFINE_GUID(GUID_NULL, 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

DEFINE_GUID(CLSID_DirectSoundPrivate, 0x11ab3ec0, 0x25ec, 0x11d1, 0xa4, 0xd8, 0x00, 0xc0, 0x4f, 0xc2, 0x8a, 0xca);

DEFINE_GUID(DSPROPSETID_DirectSoundDevice, 0x84624f82, 0x25ec, 0x11d1, 0xa4, 0xd8, 0x00, 0xc0, 0x4f, 0xc2, 0x8a, 0xca);

DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

DEFINE_DEVPROPKEY(DEVPKEY_Device_FriendlyName, 0xa45c254e,0xdf1c,0x4efd,0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0, 14);

DEFINE_GUID(DSPROPSETID_ZOOMFX_BufferProperties, 0xcd5368e0, 0x3450, 0x11d3, 0x8b, 0x6e, 0x00, 0x10, 0x5a, 0x9b, 0x7b, 0xbc);
DEFINE_GUID(DSPROPSETID_I3DL2_ListenerProperties, 0xda0f0520, 0x300a, 0x11d3, 0x8a, 0x2b, 0x00, 0x60, 0x97, 0x0d, 0xb0, 0x11);
DEFINE_GUID(DSPROPSETID_I3DL2_BufferProperties,   0xda0f0521, 0x300a, 0x11d3, 0x8a, 0x2b, 0x00, 0x60, 0x97, 0x0d, 0xb0, 0x11);

DEFINE_GUID(DSPROPSETID_VoiceManager, 0x62a69bae, 0xdf9d, 0x11d1, 0x99, 0xa6, 0x00, 0xc0, 0x4f, 0xc9, 0x9d, 0x46);


HRESULT DSPrimary_PreInit(DSPrimary *prim, DSDevice *parent);
void DSPrimary_Clear(DSPrimary *prim);
void DSPrimary_triggernots(DSPrimary *prim);
void DSPrimary_streamfeeder(DSPrimary *prim, BYTE *scratch_mem/*2K non-permanent memory*/);
HRESULT WINAPI DSPrimary_Initialize(IDirectSoundBuffer *iface, IDirectSound *ds, const DSBUFFERDESC *desc);
HRESULT WINAPI DSPrimary3D_CommitDeferredSettings(IDirectSound3DListener *iface);

HRESULT DSBuffer_Create(DSBuffer **ppv, DSPrimary *parent, IDirectSoundBuffer *orig);
void DSBuffer_Destroy(DSBuffer *buf);
HRESULT DSBuffer_GetInterface(DSBuffer *buf, REFIID riid, void **ppv);
void DSBuffer_SetParams(DSBuffer *buffer, const DS3DBUFFER *params, LONG flags);
HRESULT WINAPI DSBuffer_GetCurrentPosition(IDirectSoundBuffer8 *iface, DWORD *playpos, DWORD *curpos);
HRESULT WINAPI DSBuffer_GetStatus(IDirectSoundBuffer8 *iface, DWORD *status);
HRESULT WINAPI DSBuffer_Initialize(IDirectSoundBuffer8 *iface, IDirectSound *ds, const DSBUFFERDESC *desc);

HRESULT EAX1_Query(DSPrimary *prim, DWORD propid, ULONG *pTypeSupport);
HRESULT EAX1Buffer_Query(DSBuffer *buf, DWORD propid, ULONG *pTypeSupport);

HRESULT EAX2_Query(DSPrimary *prim, DWORD propid, ULONG *pTypeSupport);
HRESULT EAX2Buffer_Query(DSBuffer *buf, DWORD propid, ULONG *pTypeSupport);

HRESULT EAX3_Query(DSPrimary *prim, DWORD propid, ULONG *pTypeSupport);
HRESULT EAX3Buffer_Query(DSBuffer *buf, DWORD propid, ULONG *pTypeSupport);

HRESULT EAX4Context_Query(DSPrimary *prim, DWORD propid, ULONG *pTypeSupport);
HRESULT EAX4Slot_Query(DSPrimary *prim, DWORD propid, ULONG *pTypeSupport);
HRESULT EAX4Source_Query(DSBuffer *buf, DWORD propid, ULONG *pTypeSupport);

HRESULT VoiceMan_Query(DSBuffer *buf, DWORD propid, ULONG *pTypeSupport);
HRESULT VoiceMan_Set(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData);
HRESULT VoiceMan_Get(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned);

static inline LONG gain_to_mB(float gain)
{
    return (gain > 1e-5f) ? (LONG)(log10f(gain) * 2000.0f) : -10000l;
}
static inline float mB_to_gain(float millibels)
{
    return (millibels > -10000.0f) ? powf(10.0f, millibels/2000.0f) : 0.0f;
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

static inline LONG minI(LONG a, LONG b)
{ return (a < b) ? a : b; }
static inline float minF(float a, float b)
{ return (a < b) ? a : b; }

static inline float maxF(float a, float b)
{ return (a > b) ? a : b; }


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

typedef BOOL (CALLBACK *PRVTENUMCALLBACK)(EDataFlow flow, LPGUID guid, LPCWSTR descW, LPCWSTR modW, LPVOID data);
HRESULT enumerate_mmdevices(EDataFlow flow, PRVTENUMCALLBACK cb, void *user);
HRESULT get_mmdevice(EDataFlow flow, const GUID *tgt, IMMDevice **device);
void release_mmdevice(IMMDevice *device, HRESULT init_hr);

extern const WCHAR aldriver_name[];

#ifndef DECLSPEC_EXPORT
#ifdef _WIN32
#define DECLSPEC_EXPORT __declspec(dllexport)
#else
#define DECLSPEC_EXPORT
#endif
#endif

HRESULT WINAPI DSOAL_GetDeviceID(LPCGUID pGuidSrc, LPGUID pGuidDest);
