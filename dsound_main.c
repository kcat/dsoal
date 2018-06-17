/*  			DirectSound
 *
 * Copyright 1998 Marcus Meissner
 * Copyright 1998 Rob Riggs
 * Copyright 2000-2002 TransGaming Technologies, Inc.
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
 *
 * Most thread locking is complete. There may be a few race
 * conditions still lurking.
 *
 * TODO:
 *	Implement SetCooperativeLevel properly (need to address focus issues)
 *	Implement DirectSound3DBuffers (stubs in place)
 *	Use hardware 3D support if available
 *      Add critical section locking inside Release and AddRef methods
 *      Handle static buffers - put those in hardware, non-static not in hardware
 *      Hardware DuplicateSoundBuffer
 *      Proper volume calculation for 3d buffers
 *      Remove DS_HEL_FRAGS and use mixer fragment length for it
 */

#define CONST_VTABLE
#include <stdarg.h>
#include <string.h>

#include <windows.h>
#include <dsound.h>
#include <mmsystem.h>

#include "dsound_private.h"

#ifndef DECLSPEC_EXPORT
#ifdef _WIN32
#define DECLSPEC_EXPORT __declspec(dllexport)
#else
#define DECLSPEC_EXPORT
#endif
#endif

DEFINE_GUID(CLSID_DirectSoundPrivate,0x11ab3ec0,0x25ec,0x11d1,0xa4,0xd8,0x00,0xc0,0x4f,0xc2,0x8a,0xca);

int LogLevel = 1;
FILE *LogFile;


const GUID DSOUND_renderer_guid = { 0xbd6dd71a, 0x3deb, 0x11d1, { 0xb1, 0x71, 0x00, 0xc0, 0x4f, 0xc2, 0x00, 0x00 } };
const GUID DSOUND_capture_guid = { 0xbd6dd71b, 0x3deb, 0x11d1, { 0xb1, 0x71, 0x00, 0xc0, 0x4f, 0xc2, 0x00, 0x00 } };

const IID DSPROPSETID_EAX20_ListenerProperties = {
    0x0306a6a8, 0xb224, 0x11d2, { 0x99, 0xe5, 0x00, 0x00, 0xe8, 0xd8, 0xc7, 0x22 }
};

const IID DSPROPSETID_EAX20_BufferProperties = {
    0x0306a6a7, 0xb224, 0x11d2, { 0x99, 0xe5, 0x00, 0x00, 0xe8, 0xd8, 0xc7, 0x22 }
};

const EAXLISTENERPROPERTIES EnvironmentDefaults[EAX_ENVIRONMENT_COUNT] =
{
    REVERB_PRESET_GENERIC,
    REVERB_PRESET_PADDEDCELL,
    REVERB_PRESET_ROOM,
    REVERB_PRESET_BATHROOM,
    REVERB_PRESET_LIVINGROOM,
    REVERB_PRESET_STONEROOM,
    REVERB_PRESET_AUDITORIUM,
    REVERB_PRESET_CONCERTHALL,
    REVERB_PRESET_CAVE,
    REVERB_PRESET_ARENA,
    REVERB_PRESET_HANGAR,
    REVERB_PRESET_CARPETEDHALLWAY,
    REVERB_PRESET_HALLWAY,
    REVERB_PRESET_STONECORRIDOR,
    REVERB_PRESET_ALLEY,
    REVERB_PRESET_FOREST,
    REVERB_PRESET_CITY,
    REVERB_PRESET_MOUNTAINS,
    REVERB_PRESET_QUARRY,
    REVERB_PRESET_PLAIN,
    REVERB_PRESET_PARKINGLOT,
    REVERB_PRESET_SEWERPIPE,
    REVERB_PRESET_UNDERWATER,
    REVERB_PRESET_DRUGGED,
    REVERB_PRESET_DIZZY,
    REVERB_PRESET_PSYCHOTIC
};

static CRITICAL_SECTION_DEBUG openal_crst_debug =
{
    0, 0, &openal_crst,
    { &openal_crst_debug.ProcessLocksList,
      &openal_crst_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": openal_crst_debug") }
};
CRITICAL_SECTION openal_crst = { &openal_crst_debug, -1, 0, 0, 0, 0 };

int openal_loaded = 0;
static HANDLE openal_handle = NULL;
LPALCCREATECONTEXT palcCreateContext = NULL;
LPALCMAKECONTEXTCURRENT palcMakeContextCurrent = NULL;
LPALCPROCESSCONTEXT palcProcessContext = NULL;
LPALCSUSPENDCONTEXT palcSuspendContext = NULL;
LPALCDESTROYCONTEXT palcDestroyContext = NULL;
LPALCGETCURRENTCONTEXT palcGetCurrentContext = NULL;
LPALCGETCONTEXTSDEVICE palcGetContextsDevice = NULL;
LPALCOPENDEVICE palcOpenDevice = NULL;
LPALCCLOSEDEVICE palcCloseDevice = NULL;
LPALCGETERROR palcGetError = NULL;
LPALCISEXTENSIONPRESENT palcIsExtensionPresent = NULL;
LPALCGETPROCADDRESS palcGetProcAddress = NULL;
LPALCGETENUMVALUE palcGetEnumValue = NULL;
LPALCGETSTRING palcGetString = NULL;
LPALCGETINTEGERV palcGetIntegerv = NULL;
LPALCCAPTUREOPENDEVICE palcCaptureOpenDevice = NULL;
LPALCCAPTURECLOSEDEVICE palcCaptureCloseDevice = NULL;
LPALCCAPTURESTART palcCaptureStart = NULL;
LPALCCAPTURESTOP palcCaptureStop = NULL;
LPALCCAPTURESAMPLES palcCaptureSamples = NULL;
LPALENABLE palEnable = NULL;
LPALDISABLE palDisable = NULL;
LPALISENABLED palIsEnabled = NULL;
LPALGETSTRING palGetString = NULL;
LPALGETBOOLEANV palGetBooleanv = NULL;
LPALGETINTEGERV palGetIntegerv = NULL;
LPALGETFLOATV palGetFloatv = NULL;
LPALGETDOUBLEV palGetDoublev = NULL;
LPALGETBOOLEAN palGetBoolean = NULL;
LPALGETINTEGER palGetInteger = NULL;
LPALGETFLOAT palGetFloat = NULL;
LPALGETDOUBLE palGetDouble = NULL;
LPALGETERROR palGetError = NULL;
LPALISEXTENSIONPRESENT palIsExtensionPresent = NULL;
LPALGETPROCADDRESS palGetProcAddress = NULL;
LPALGETENUMVALUE palGetEnumValue = NULL;
LPALLISTENERF palListenerf = NULL;
LPALLISTENER3F palListener3f = NULL;
LPALLISTENERFV palListenerfv = NULL;
LPALLISTENERI palListeneri = NULL;
LPALLISTENER3I palListener3i = NULL;
LPALLISTENERIV palListeneriv = NULL;
LPALGETLISTENERF palGetListenerf = NULL;
LPALGETLISTENER3F palGetListener3f = NULL;
LPALGETLISTENERFV palGetListenerfv = NULL;
LPALGETLISTENERI palGetListeneri = NULL;
LPALGETLISTENER3I palGetListener3i = NULL;
LPALGETLISTENERIV palGetListeneriv = NULL;
LPALGENSOURCES palGenSources = NULL;
LPALDELETESOURCES palDeleteSources = NULL;
LPALISSOURCE palIsSource = NULL;
LPALSOURCEF palSourcef = NULL;
LPALSOURCE3F palSource3f = NULL;
LPALSOURCEFV palSourcefv = NULL;
LPALSOURCEI palSourcei = NULL;
LPALSOURCE3I palSource3i = NULL;
LPALSOURCEIV palSourceiv = NULL;
LPALGETSOURCEF palGetSourcef = NULL;
LPALGETSOURCE3F palGetSource3f = NULL;
LPALGETSOURCEFV palGetSourcefv = NULL;
LPALGETSOURCEI palGetSourcei = NULL;
LPALGETSOURCE3I palGetSource3i = NULL;
LPALGETSOURCEIV palGetSourceiv = NULL;
LPALSOURCEPLAYV palSourcePlayv = NULL;
LPALSOURCESTOPV palSourceStopv = NULL;
LPALSOURCEREWINDV palSourceRewindv = NULL;
LPALSOURCEPAUSEV palSourcePausev = NULL;
LPALSOURCEPLAY palSourcePlay = NULL;
LPALSOURCESTOP palSourceStop = NULL;
LPALSOURCEREWIND palSourceRewind = NULL;
LPALSOURCEPAUSE palSourcePause = NULL;
LPALSOURCEQUEUEBUFFERS palSourceQueueBuffers = NULL;
LPALSOURCEUNQUEUEBUFFERS palSourceUnqueueBuffers = NULL;
LPALGENBUFFERS palGenBuffers = NULL;
LPALDELETEBUFFERS palDeleteBuffers = NULL;
LPALISBUFFER palIsBuffer = NULL;
LPALBUFFERF palBufferf = NULL;
LPALBUFFER3F palBuffer3f = NULL;
LPALBUFFERFV palBufferfv = NULL;
LPALBUFFERI palBufferi = NULL;
LPALBUFFER3I palBuffer3i = NULL;
LPALBUFFERIV palBufferiv = NULL;
LPALGETBUFFERF palGetBufferf = NULL;
LPALGETBUFFER3F palGetBuffer3f = NULL;
LPALGETBUFFERFV palGetBufferfv = NULL;
LPALGETBUFFERI palGetBufferi = NULL;
LPALGETBUFFER3I palGetBuffer3i = NULL;
LPALGETBUFFERIV palGetBufferiv = NULL;
LPALBUFFERDATA palBufferData = NULL;
LPALDOPPLERFACTOR palDopplerFactor = NULL;
LPALDOPPLERVELOCITY palDopplerVelocity = NULL;
LPALDISTANCEMODEL palDistanceModel = NULL;
LPALSPEEDOFSOUND palSpeedOfSound = NULL;

LPALCMAKECONTEXTCURRENT set_context;
LPALCGETCURRENTCONTEXT get_context;
BOOL local_contexts;

static void EnterALSectionTLS(ALCcontext *ctx);
static void LeaveALSectionTLS(void);
static void EnterALSectionGlob(ALCcontext *ctx);
static void LeaveALSectionGlob(void);

DWORD TlsThreadPtr;
void (*EnterALSection)(ALCcontext *ctx) = EnterALSectionGlob;
void (*LeaveALSection)(void) = LeaveALSectionGlob;


static BOOL load_libopenal(void)
{
    const char libname[] = "dsoal-aldrv.dll";
    BOOL failed = FALSE;
    const char *str;

    str = getenv("DSOAL_LOGLEVEL");
    if(str && *str)
        LogLevel = atoi(str);

    openal_handle = LoadLibraryA(libname);
    if(!openal_handle)
    {
        ERR("Couldn't load %s: %lu\n", libname, GetLastError());
        return FALSE;
    }

#define LOAD_FUNCPTR(f) do {                                           \
    if((*((void**)&p##f) = GetProcAddress(openal_handle, #f)) == NULL) \
    {                                                                  \
        ERR("Couldn't lookup %s in %s\n", #f, libname);                \
        failed = TRUE;                                                 \
    }                                                                  \
} while(0)

    LOAD_FUNCPTR(alcCreateContext);
    LOAD_FUNCPTR(alcMakeContextCurrent);
    LOAD_FUNCPTR(alcProcessContext);
    LOAD_FUNCPTR(alcSuspendContext);
    LOAD_FUNCPTR(alcDestroyContext);
    LOAD_FUNCPTR(alcGetCurrentContext);
    LOAD_FUNCPTR(alcGetContextsDevice);
    LOAD_FUNCPTR(alcOpenDevice);
    LOAD_FUNCPTR(alcCloseDevice);
    LOAD_FUNCPTR(alcGetError);
    LOAD_FUNCPTR(alcIsExtensionPresent);
    LOAD_FUNCPTR(alcGetProcAddress);
    LOAD_FUNCPTR(alcGetEnumValue);
    LOAD_FUNCPTR(alcGetString);
    LOAD_FUNCPTR(alcGetIntegerv);
    LOAD_FUNCPTR(alcCaptureOpenDevice);
    LOAD_FUNCPTR(alcCaptureCloseDevice);
    LOAD_FUNCPTR(alcCaptureStart);
    LOAD_FUNCPTR(alcCaptureStop);
    LOAD_FUNCPTR(alcCaptureSamples);
    LOAD_FUNCPTR(alEnable);
    LOAD_FUNCPTR(alDisable);
    LOAD_FUNCPTR(alIsEnabled);
    LOAD_FUNCPTR(alGetString);
    LOAD_FUNCPTR(alGetBooleanv);
    LOAD_FUNCPTR(alGetIntegerv);
    LOAD_FUNCPTR(alGetFloatv);
    LOAD_FUNCPTR(alGetDoublev);
    LOAD_FUNCPTR(alGetBoolean);
    LOAD_FUNCPTR(alGetInteger);
    LOAD_FUNCPTR(alGetFloat);
    LOAD_FUNCPTR(alGetDouble);
    LOAD_FUNCPTR(alGetError);
    LOAD_FUNCPTR(alIsExtensionPresent);
    LOAD_FUNCPTR(alGetProcAddress);
    LOAD_FUNCPTR(alGetEnumValue);
    LOAD_FUNCPTR(alListenerf);
    LOAD_FUNCPTR(alListener3f);
    LOAD_FUNCPTR(alListenerfv);
    LOAD_FUNCPTR(alListeneri);
    LOAD_FUNCPTR(alListener3i);
    LOAD_FUNCPTR(alListeneriv);
    LOAD_FUNCPTR(alGetListenerf);
    LOAD_FUNCPTR(alGetListener3f);
    LOAD_FUNCPTR(alGetListenerfv);
    LOAD_FUNCPTR(alGetListeneri);
    LOAD_FUNCPTR(alGetListener3i);
    LOAD_FUNCPTR(alGetListeneriv);
    LOAD_FUNCPTR(alGenSources);
    LOAD_FUNCPTR(alDeleteSources);
    LOAD_FUNCPTR(alIsSource);
    LOAD_FUNCPTR(alSourcef);
    LOAD_FUNCPTR(alSource3f);
    LOAD_FUNCPTR(alSourcefv);
    LOAD_FUNCPTR(alSourcei);
    LOAD_FUNCPTR(alSource3i);
    LOAD_FUNCPTR(alSourceiv);
    LOAD_FUNCPTR(alGetSourcef);
    LOAD_FUNCPTR(alGetSource3f);
    LOAD_FUNCPTR(alGetSourcefv);
    LOAD_FUNCPTR(alGetSourcei);
    LOAD_FUNCPTR(alGetSource3i);
    LOAD_FUNCPTR(alGetSourceiv);
    LOAD_FUNCPTR(alSourcePlayv);
    LOAD_FUNCPTR(alSourceStopv);
    LOAD_FUNCPTR(alSourceRewindv);
    LOAD_FUNCPTR(alSourcePausev);
    LOAD_FUNCPTR(alSourcePlay);
    LOAD_FUNCPTR(alSourceStop);
    LOAD_FUNCPTR(alSourceRewind);
    LOAD_FUNCPTR(alSourcePause);
    LOAD_FUNCPTR(alSourceQueueBuffers);
    LOAD_FUNCPTR(alSourceUnqueueBuffers);
    LOAD_FUNCPTR(alGenBuffers);
    LOAD_FUNCPTR(alDeleteBuffers);
    LOAD_FUNCPTR(alIsBuffer);
    LOAD_FUNCPTR(alBufferf);
    LOAD_FUNCPTR(alBuffer3f);
    LOAD_FUNCPTR(alBufferfv);
    LOAD_FUNCPTR(alBufferi);
    LOAD_FUNCPTR(alBuffer3i);
    LOAD_FUNCPTR(alBufferiv);
    LOAD_FUNCPTR(alGetBufferf);
    LOAD_FUNCPTR(alGetBuffer3f);
    LOAD_FUNCPTR(alGetBufferfv);
    LOAD_FUNCPTR(alGetBufferi);
    LOAD_FUNCPTR(alGetBuffer3i);
    LOAD_FUNCPTR(alGetBufferiv);
    LOAD_FUNCPTR(alBufferData);
    LOAD_FUNCPTR(alDopplerFactor);
    LOAD_FUNCPTR(alDopplerVelocity);
    LOAD_FUNCPTR(alDistanceModel);
    LOAD_FUNCPTR(alSpeedOfSound);
#undef LOAD_FUNCPTR
    if (failed)
    {
        WARN("Unloading %s\n", libname);
        if (openal_handle != NULL)
            FreeLibrary(openal_handle);
        openal_handle = NULL;
        return FALSE;
    }

    openal_loaded = 1;
    TRACE("Loaded %s\n", libname);

    local_contexts = alcIsExtensionPresent(NULL, "ALC_EXT_thread_local_context");
    if(local_contexts)
    {
        TRACE("Found ALC_EXT_thread_local_context\n");

        set_context = alcGetProcAddress(NULL, "alcSetThreadContext");
        get_context = alcGetProcAddress(NULL, "alcGetThreadContext");
        if(!set_context || !get_context)
        {
            ERR("TLS advertised but functions not found, disabling thread local contexts\n");
            local_contexts = 0;
        }
    }
    if(!local_contexts)
    {
        set_context = alcMakeContextCurrent;
        get_context = alcGetCurrentContext;
    }
    else
    {
        EnterALSection = EnterALSectionTLS;
        LeaveALSection = LeaveALSectionTLS;
    }

    return TRUE;
}


static void EnterALSectionTLS(ALCcontext *ctx)
{
    if(LIKELY(ctx == TlsGetValue(TlsThreadPtr)))
        return;

    if(LIKELY(set_context(ctx) != ALC_FALSE))
        TlsSetValue(TlsThreadPtr, ctx);
    else
    {
        ERR("Couldn't set current context!!\n");
        checkALCError(alcGetContextsDevice(ctx));
    }
}
static void LeaveALSectionTLS(void)
{
}

static void EnterALSectionGlob(ALCcontext *ctx)
{
    EnterCriticalSection(&openal_crst);
    if(UNLIKELY(alcMakeContextCurrent(ctx) == ALC_FALSE))
    {
        ERR("Couldn't set current context!!\n");
        checkALCError(alcGetContextsDevice(ctx));
    }
}
static void LeaveALSectionGlob(void)
{
    LeaveCriticalSection(&openal_crst);
}


const ALCchar *DSOUND_getdevicestrings(void)
{
    if(alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT"))
        return alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
    return alcGetString(NULL, ALC_DEVICE_SPECIFIER);
}

const ALCchar *DSOUND_getcapturedevicestrings(void)
{
    return alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER);
}

/***************************************************************************
 * GetDeviceID	[DSOUND.9]
 *
 * Retrieves unique identifier of default device specified
 *
 * PARAMS
 *    pGuidSrc  [I] Address of device GUID.
 *    pGuidDest [O] Address to receive unique device GUID.
 *
 * RETURNS
 *    Success: DS_OK
 *    Failure: DSERR_INVALIDPARAM
 *
 * NOTES
 *    pGuidSrc is a valid device GUID or DSDEVID_DefaultPlayback,
 *    DSDEVID_DefaultCapture, DSDEVID_DefaultVoicePlayback, or
 *    DSDEVID_DefaultVoiceCapture.
 *    Returns pGuidSrc if pGuidSrc is a valid device or the device
 *    GUID for the specified constants.
 */
DECLSPEC_EXPORT HRESULT WINAPI GetDeviceID(LPCGUID pGuidSrc, LPGUID pGuidDest)
{
    TRACE("(%s, %p)\n", debugstr_guid(pGuidSrc), pGuidDest);

    if(pGuidSrc == NULL)
    {
        WARN("invalid parameter: pGuidSrc == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    if(pGuidDest == NULL)
    {
        WARN("invalid parameter: pGuidDest == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    if(IsEqualGUID(&DSDEVID_DefaultPlayback, pGuidSrc) ||
       IsEqualGUID(&DSDEVID_DefaultVoicePlayback, pGuidSrc))
        *pGuidDest = DSOUND_renderer_guid;
    else if(IsEqualGUID(&DSDEVID_DefaultCapture, pGuidSrc) ||
            IsEqualGUID(&DSDEVID_DefaultVoiceCapture, pGuidSrc))
        *pGuidDest = DSOUND_capture_guid;
    else
        *pGuidDest = *pGuidSrc;

    TRACE("returns %s\n", debugstr_guid(pGuidDest));
    return DS_OK;
}

struct ConvAcontext
{
    LPDSENUMCALLBACKA callA;
    LPVOID data;

    CHAR *str;
    int strlen;
};

static BOOL CALLBACK w2a_callback(LPGUID guid, LPCWSTR descW, LPCWSTR modW, LPVOID data)
{
    struct ConvAcontext *context = data;
    int desclen, modlen;
    CHAR *descA, *modA;

    desclen = WideCharToMultiByte(CP_ACP, 0, descW, -1, NULL, 0, NULL, NULL);
    modlen = WideCharToMultiByte(CP_ACP, 0, modW, -1, NULL, 0, NULL, NULL);

    if(desclen+modlen > context->strlen)
    {
        CHAR *newstr = realloc(context->str, desclen+modlen+2);
        if(!newstr) return FALSE;

        context->str = newstr;
        context->strlen = desclen+modlen;
    }

    descA = context->str;
    modA = context->str + desclen+1;

    WideCharToMultiByte(CP_ACP, 0, descW, -1, descA, desclen, NULL, NULL);
    descA[desclen] = 0;
    WideCharToMultiByte(CP_ACP, 0, modW, -1, modA, modlen, NULL, NULL);
    modA[modlen] = 0;

    return context->callA(guid, descA, modA, context->data);
}

/***************************************************************************
 * DirectSoundEnumerateA [DSOUND.2]
 *
 * Enumerate all DirectSound drivers installed in the system
 *
 * PARAMS
 *    lpDSEnumCallback  [I] Address of callback function.
 *    lpContext         [I] Address of user defined context passed to callback function.
 *
 * RETURNS
 *    Success: DS_OK
 *    Failure: DSERR_INVALIDPARAM
 */
DECLSPEC_EXPORT HRESULT WINAPI DirectSoundEnumerateA(
    LPDSENUMCALLBACKA lpDSEnumCallback,
    LPVOID lpContext)
{
    struct ConvAcontext context;
    HRESULT ret;

    TRACE("lpDSEnumCallback = %p, lpContext = %p\n", lpDSEnumCallback, lpContext);

    if(lpDSEnumCallback == NULL) {
        WARN("invalid parameter: lpDSEnumCallback == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    context.callA = lpDSEnumCallback;
    context.data = lpContext;
    context.str = NULL;
    context.strlen = 0;

    ret = DirectSoundEnumerateW(w2a_callback, &context);

    free(context.str);
    return ret;
}

/***************************************************************************
 * DirectSoundEnumerateW [DSOUND.3]
 *
 * Enumerate all DirectSound drivers installed in the system
 *
 * PARAMS
 *    lpDSEnumCallback  [I] Address of callback function.
 *    lpContext         [I] Address of user defined context passed to callback function.
 *
 * RETURNS
 *    Success: DS_OK
 *    Failure: DSERR_INVALIDPARAM
 */
DECLSPEC_EXPORT HRESULT WINAPI DirectSoundEnumerateW(
    LPDSENUMCALLBACKW lpDSEnumCallback,
    LPVOID lpContext )
{
    TRACE("lpDSEnumCallback = %p, lpContext = %p\n", lpDSEnumCallback, lpContext);

    if(!lpDSEnumCallback)
    {
        WARN("invalid parameter: lpDSEnumCallback == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    if(!openal_loaded)
    {
        ERR("Attempting to enumerate sound cards without OpenAL support\n");
        ERR("Please recompile wine with OpenAL for sound to work\n");
    }
    else
    {
        WCHAR *devwstr = NULL;
        int devwstrlen = 0;
        const ALCchar *devstr;
        GUID guid;

        EnterCriticalSection(&openal_crst);
        devstr = DSOUND_getdevicestrings();
        if(!devstr || !*devstr)
            goto out;

        TRACE("calling lpDSEnumCallback(NULL,\"%ls\",\"%ls\",%p)\n",
              L"Primary Sound Driver", L"", lpContext);
        if(lpDSEnumCallback(NULL, L"Primary Sound Driver", L"", lpContext) == FALSE)
            goto out;

        guid = DSOUND_renderer_guid;
        do {
            int len;

            len = MultiByteToWideChar(CP_UTF8, 0, devstr, -1, NULL, 0);
            if(len <= 0) break;

            if(len > devwstrlen)
            {
                WCHAR *str = realloc(devwstr, (len+1)*sizeof(WCHAR));
                if(!str) break;

                devwstr = str;
                devwstrlen = len;
            }
            MultiByteToWideChar(CP_UTF8, 0, devstr, -1, devwstr, len);
            devwstr[len] = 0;

            TRACE("calling lpDSEnumCallback(%s,\"%ls\",\"%ls\",%p)\n",
                  debugstr_guid(&guid), devwstr, L"wineal.drv", lpContext);
            if(lpDSEnumCallback(&guid, devwstr, L"wineal.drv", lpContext) == FALSE)
                goto out;

            guid.Data4[7]++;
            devstr += strlen(devstr)+1;
        } while(*devstr);
out:
        LeaveCriticalSection(&openal_crst);
        free(devwstr);
    }
    return DS_OK;
}

/***************************************************************************
 * DirectSoundCaptureEnumerateA [DSOUND.7]
 *
 * Enumerate all DirectSound drivers installed in the system.
 *
 * PARAMS
 *    lpDSEnumCallback  [I] Address of callback function.
 *    lpContext         [I] Address of user defined context passed to callback function.
 *
 * RETURNS
 *    Success: DS_OK
 *    Failure: DSERR_INVALIDPARAM
 */
DECLSPEC_EXPORT HRESULT WINAPI DirectSoundCaptureEnumerateA(
    LPDSENUMCALLBACKA lpDSEnumCallback,
    LPVOID lpContext)
{
    struct ConvAcontext context;
    HRESULT ret;

    TRACE("lpDSEnumCallback = %p, lpContext = %p\n", lpDSEnumCallback, lpContext);

    if(lpDSEnumCallback == NULL) {
        WARN("invalid parameter: lpDSEnumCallback == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    context.callA = lpDSEnumCallback;
    context.data = lpContext;
    context.str = NULL;
    context.strlen = 0;

    ret = DirectSoundCaptureEnumerateW(w2a_callback, &context);

    free(context.str);
    return ret;
}

/***************************************************************************
 * DirectSoundCaptureEnumerateW [DSOUND.8]
 *
 * Enumerate all DirectSound drivers installed in the system.
 *
 * PARAMS
 *    lpDSEnumCallback  [I] Address of callback function.
 *    lpContext         [I] Address of user defined context passed to callback function.
 *
 * RETURNS
 *    Success: DS_OK
 *    Failure: DSERR_INVALIDPARAM
 */
DECLSPEC_EXPORT HRESULT WINAPI DirectSoundCaptureEnumerateW(
    LPDSENUMCALLBACKW lpDSEnumCallback,
    LPVOID lpContext)
{
    TRACE("lpDSEnumCallback = %p, lpContext = %p\n", lpDSEnumCallback, lpContext);

    if(!lpDSEnumCallback)
    {
        WARN("invalid parameter: lpDSEnumCallback == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    if(!openal_loaded)
    {
        ERR("Attempting to enumerate sound cards without OpenAL support\n");
        ERR("Please recompile wine with OpenAL for sound to work\n");
    }
    else
    {
        WCHAR *devwstr = NULL;
        int devwstrlen = 0;
        const ALCchar *devstr;
        GUID guid;

        EnterCriticalSection(&openal_crst);
        devstr = DSOUND_getcapturedevicestrings();
        if(!devstr || !*devstr)
            goto out;

        TRACE("calling lpDSEnumCallback(NULL,\"%ls\",\"%ls\",%p)\n",
              L"Primary Sound Driver", L"", lpContext);
        if(lpDSEnumCallback(NULL, L"Primary Sound Driver", L"", lpContext) == FALSE)
            goto out;

        guid = DSOUND_capture_guid;
        do {
            int len;

            len = MultiByteToWideChar(CP_UTF8, 0, devstr, -1, NULL, 0);
            if(len <= 0) break;

            if(len > devwstrlen)
            {
                WCHAR *str = realloc(devwstr, (len+1)*sizeof(WCHAR));
                if(!str) break;

                devwstr = str;
                devwstrlen = len;
            }
            MultiByteToWideChar(CP_UTF8, 0, devstr, -1, devwstr, len);
            devwstr[len] = 0;

            TRACE("calling lpDSEnumCallback(%s,\"%ls\",\"%ls\",%p)\n",
                  debugstr_guid(&guid), devwstr, L"wineal.drv", lpContext);
            if(lpDSEnumCallback(&guid, devwstr, L"wineal.drv", lpContext) == FALSE)
                goto out;

            guid.Data4[7]++;
            devstr += strlen(devstr)+1;
        } while(*devstr);
out:
        LeaveCriticalSection(&openal_crst);
        free(devwstr);
    }

    return DS_OK;
}

/*******************************************************************************
 *      DirectSoundCreate (DSOUND.1)
 *
 *  Creates and initializes a DirectSound interface.
 *
 *  PARAMS
 *     lpcGUID   [I] Address of the GUID that identifies the sound device.
 *     ppDS      [O] Address of a variable to receive the interface pointer.
 *     pUnkOuter [I] Must be NULL.
 *
 *  RETURNS
 *     Success: DS_OK
 *     Failure: DSERR_ALLOCATED, DSERR_INVALIDPARAM, DSERR_NOAGGREGATION,
 *              DSERR_NODRIVER, DSERR_OUTOFMEMORY
 */
DECLSPEC_EXPORT HRESULT WINAPI
DirectSoundCreate(LPCGUID lpcGUID, IDirectSound **ppDS, IUnknown *pUnkOuter)
{
    HRESULT hr;
    void *pDS;

    TRACE("(%s, %p, %p)\n", debugstr_guid(lpcGUID), ppDS, pUnkOuter);

    if (ppDS == NULL) {
        WARN("invalid parameter: ppDS == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ppDS = NULL;

    if (pUnkOuter != NULL) {
        WARN("invalid parameter: pUnkOuter != NULL\n");
        return DSERR_INVALIDPARAM;
    }

    hr = DSOUND_Create(&IID_IDirectSound, &pDS);
    if(SUCCEEDED(hr))
    {
        *ppDS = pDS;
        hr = IDirectSound_Initialize(*ppDS, lpcGUID);
        if(FAILED(hr))
        {
            IDirectSound_Release(*ppDS);
            *ppDS = NULL;
        }
    }

    return hr;
}

/*******************************************************************************
 *        DirectSoundCreate8 (DSOUND.11)
 *
 *  Creates and initializes a DirectSound8 interface.
 *
 *  PARAMS
 *     lpcGUID   [I] Address of the GUID that identifies the sound device.
 *     ppDS      [O] Address of a variable to receive the interface pointer.
 *     pUnkOuter [I] Must be NULL.
 *
 *  RETURNS
 *     Success: DS_OK
 *     Failure: DSERR_ALLOCATED, DSERR_INVALIDPARAM, DSERR_NOAGGREGATION,
 *              DSERR_NODRIVER, DSERR_OUTOFMEMORY
 */
DECLSPEC_EXPORT HRESULT WINAPI
DirectSoundCreate8(LPCGUID lpcGUID, IDirectSound8 **ppDS, IUnknown *pUnkOuter)
{
    HRESULT hr;
    void *pDS;

    TRACE("(%s, %p, %p)\n", debugstr_guid(lpcGUID), ppDS, pUnkOuter);

    if (ppDS == NULL) {
        WARN("invalid parameter: ppDS == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ppDS = NULL;

    if (pUnkOuter != NULL) {
        WARN("invalid parameter: pUnkOuter != NULL\n");
        return DSERR_INVALIDPARAM;
    }

    hr = DSOUND_Create8(&IID_IDirectSound8, &pDS);
    if(SUCCEEDED(hr))
    {
        *ppDS = pDS;
        hr = IDirectSound8_Initialize(*ppDS, lpcGUID);
        if(FAILED(hr))
        {
            IDirectSound8_Release(*ppDS);
            *ppDS = NULL;
        }
    }

    return hr;
}

/***************************************************************************
 * DirectSoundCaptureCreate [DSOUND.6]
 *
 * Create and initialize a DirectSoundCapture interface.
 *
 * PARAMS
 *    lpcGUID   [I] Address of the GUID that identifies the sound capture device.
 *    lplpDSC   [O] Address of a variable to receive the interface pointer.
 *    pUnkOuter [I] Must be NULL.
 *
 * RETURNS
 *    Success: DS_OK
 *    Failure: DSERR_NOAGGREGATION, DSERR_ALLOCATED, DSERR_INVALIDPARAM,
 *             DSERR_OUTOFMEMORY
 *
 * NOTES
 *    lpcGUID must be one of the values returned from DirectSoundCaptureEnumerate
 *    or NULL for the default device or DSDEVID_DefaultCapture or
 *    DSDEVID_DefaultVoiceCapture.
 *
 *    DSERR_ALLOCATED is returned for sound devices that do not support full duplex.
 */
DECLSPEC_EXPORT HRESULT WINAPI
DirectSoundCaptureCreate(LPCGUID lpcGUID, IDirectSoundCapture **ppDSC, IUnknown *pUnkOuter)
{
    HRESULT hr;
    void *pDSC;

    TRACE("(%s, %p, %p)\n", debugstr_guid(lpcGUID), ppDSC, pUnkOuter);

    if(ppDSC == NULL)
    {
        WARN("invalid parameter: ppDSC == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ppDSC = NULL;

    if(pUnkOuter)
    {
        WARN("invalid parameter: pUnkOuter != NULL\n");
        return DSERR_NOAGGREGATION;
    }

    hr = DSOUND_CaptureCreate(&IID_IDirectSoundCapture, &pDSC);
    if(SUCCEEDED(hr))
    {
        *ppDSC = pDSC;
        hr = IDirectSoundCapture_Initialize(*ppDSC, lpcGUID);
        if(FAILED(hr))
        {
            IDirectSoundCapture_Release(*ppDSC);
            *ppDSC = NULL;
        }
    }

    return hr;
}

/***************************************************************************
 * DirectSoundCaptureCreate8 [DSOUND.12]
 *
 * Create and initialize a DirectSoundCapture interface.
 *
 * PARAMS
 *    lpcGUID   [I] Address of the GUID that identifies the sound capture device.
 *    lplpDSC   [O] Address of a variable to receive the interface pointer.
 *    pUnkOuter [I] Must be NULL.
 *
 * RETURNS
 *    Success: DS_OK
 *    Failure: DSERR_NOAGGREGATION, DSERR_ALLOCATED, DSERR_INVALIDPARAM,
 *             DSERR_OUTOFMEMORY
 *
 * NOTES
 *    lpcGUID must be one of the values returned from DirectSoundCaptureEnumerate
 *    or NULL for the default device or DSDEVID_DefaultCapture or
 *    DSDEVID_DefaultVoiceCapture.
 *
 *    DSERR_ALLOCATED is returned for sound devices that do not support full duplex.
 */
DECLSPEC_EXPORT HRESULT WINAPI
DirectSoundCaptureCreate8(LPCGUID lpcGUID, IDirectSoundCapture8 **ppDSC8, IUnknown *pUnkOuter)
{
    HRESULT hr;
    void *pDSC8;

    TRACE("(%s, %p, %p)\n", debugstr_guid(lpcGUID), ppDSC8, pUnkOuter);

    if(ppDSC8 == NULL)
    {
        WARN("invalid parameter: ppDSC8 == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ppDSC8 = NULL;

    if(pUnkOuter)
    {
        WARN("invalid parameter: pUnkOuter != NULL\n");
        return DSERR_NOAGGREGATION;
    }

    hr = DSOUND_CaptureCreate8(&IID_IDirectSoundCapture, &pDSC8);
    if(SUCCEEDED(hr))
    {
        *ppDSC8 = pDSC8;
        hr = IDirectSoundCapture_Initialize(*ppDSC8, lpcGUID);
        if(FAILED(hr))
        {
            IDirectSoundCapture_Release(*ppDSC8);
            *ppDSC8 = NULL;
        }
    }

    return hr;
}

/*******************************************************************************
 * DirectSound ClassFactory
 */

typedef  HRESULT (*FnCreateInstance)(REFIID riid, LPVOID *ppobj);

typedef struct {
    IClassFactory IClassFactory_iface;
    LONG ref;
    REFCLSID rclsid;
    FnCreateInstance pfnCreateInstance;
} IClassFactoryImpl;

static inline IClassFactoryImpl *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, IClassFactoryImpl, IClassFactory_iface);
}

static HRESULT WINAPI DSCF_QueryInterface(LPCLASSFACTORY iface, REFIID riid, LPVOID *ppobj)
{
    IClassFactoryImpl *This = impl_from_IClassFactory(iface);
    TRACE("(%p, %s, %p)\n", This, debugstr_guid(riid), ppobj);
    if (ppobj == NULL)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IClassFactory))
    {
        *ppobj = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }
    *ppobj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI DSCF_AddRef(LPCLASSFACTORY iface)
{
    IClassFactoryImpl *This = impl_from_IClassFactory(iface);
    ULONG ref = InterlockedIncrement(&(This->ref));
    TRACE("(%p) ref was %lu\n", This, ref - 1);
    return ref;
}

static ULONG WINAPI DSCF_Release(LPCLASSFACTORY iface)
{
    IClassFactoryImpl *This = impl_from_IClassFactory(iface);
    ULONG ref = InterlockedDecrement(&(This->ref));
    TRACE("(%p) ref was %lu\n", This, ref + 1);
    /* static class, won't be freed */
    return ref;
}

static HRESULT WINAPI DSCF_CreateInstance(
    LPCLASSFACTORY iface,
    LPUNKNOWN pOuter,
    REFIID riid,
    LPVOID *ppobj)
{
    IClassFactoryImpl *This = impl_from_IClassFactory(iface);
    TRACE("(%p, %p, %s, %p)\n", This, pOuter, debugstr_guid(riid), ppobj);

    if (pOuter)
        return CLASS_E_NOAGGREGATION;

    if (ppobj == NULL) {
        WARN("invalid parameter\n");
        return DSERR_INVALIDPARAM;
    }
    *ppobj = NULL;
    return This->pfnCreateInstance(riid, ppobj);
}

static HRESULT WINAPI DSCF_LockServer(LPCLASSFACTORY iface, BOOL dolock)
{
    IClassFactoryImpl *This = impl_from_IClassFactory(iface);
    FIXME("(%p, %d) stub!\n", This, dolock);
    return S_OK;
}

static const IClassFactoryVtbl DSCF_Vtbl = {
    DSCF_QueryInterface,
    DSCF_AddRef,
    DSCF_Release,
    DSCF_CreateInstance,
    DSCF_LockServer
};

static IClassFactoryImpl DSOUND_CF[] = {
    { {&DSCF_Vtbl}, 1, &CLSID_DirectSound, DSOUND_Create },
    { {&DSCF_Vtbl}, 1, &CLSID_DirectSound8, DSOUND_Create8 },
    { {&DSCF_Vtbl}, 1, &CLSID_DirectSoundCapture, DSOUND_CaptureCreate },
    { {&DSCF_Vtbl}, 1, &CLSID_DirectSoundCapture8, DSOUND_CaptureCreate8 },
    { {&DSCF_Vtbl}, 1, &CLSID_DirectSoundFullDuplex, DSOUND_FullDuplexCreate },
    { {&DSCF_Vtbl}, 1, &CLSID_DirectSoundPrivate, IKsPrivatePropertySetImpl_Create },
    { {NULL}, 0, NULL, NULL }
};

/*******************************************************************************
 * DllGetClassObject [DSOUND.@]
 * Retrieves class object from a DLL object
 *
 * NOTES
 *    Docs say returns STDAPI
 *
 * PARAMS
 *    rclsid [I] CLSID for the class object
 *    riid   [I] Reference to identifier of interface for class object
 *    ppv    [O] Address of variable to receive interface pointer for riid
 *
 * RETURNS
 *    Success: S_OK
 *    Failure: CLASS_E_CLASSNOTAVAILABLE, E_OUTOFMEMORY, E_INVALIDARG,
 *             E_UNEXPECTED
 */
DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    int i = 0;
    TRACE("(%s, %s, %p)\n", debugstr_guid(rclsid), debugstr_guid(riid), ppv);

    if (ppv == NULL) {
        WARN("invalid parameter\n");
        return E_INVALIDARG;
    }

    *ppv = NULL;

    if (!IsEqualIID(riid, &IID_IClassFactory) &&
        !IsEqualIID(riid, &IID_IUnknown)) {
        WARN("no interface for %s\n", debugstr_guid(riid));
        return E_NOINTERFACE;
    }

    while (NULL != DSOUND_CF[i].rclsid) {
        if (IsEqualGUID(rclsid, DSOUND_CF[i].rclsid)) {
            DSCF_AddRef(&DSOUND_CF[i].IClassFactory_iface);
            *ppv = &DSOUND_CF[i];
            return S_OK;
        }
        i++;
    }

    WARN("(%s, %s, %p): no class found.\n", debugstr_guid(rclsid),
         debugstr_guid(riid), ppv);
    return CLASS_E_CLASSNOTAVAILABLE;
}


/*******************************************************************************
 * DllCanUnloadNow [DSOUND.4]
 * Determines whether the DLL is in use.
 *
 * RETURNS
 *    Success: S_OK
 *    Failure: S_FALSE
 */
DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    FIXME("(void): stub\n");
    return S_FALSE;
}

/***********************************************************************
 *           DllMain (DSOUND.init)
 */
DECLSPEC_EXPORT BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    const WCHAR *wstr;

    TRACE("(%p, %lu, %p)\n", hInstDLL, fdwReason, lpvReserved);

    switch(fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        LogFile = stderr;
        if((wstr=_wgetenv(L"DSOAL_LOGFILE")) != NULL && wstr[0] != 0)
        {
            FILE *f = _wfopen(wstr, L"wt");
            if(!f) ERR("Failed to open log file %ls\n", wstr);
            else LogFile = f;
        }

        TRACE("DLL_PROCESS_ATTACH\n");
        if(!load_libopenal())
            return FALSE;
        TlsThreadPtr = TlsAlloc();
        /* Increase refcount on dsound by 1 */
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)hInstDLL, &hInstDLL);
        break;

    case DLL_THREAD_ATTACH:
        TRACE("DLL_THREAD_ATTACH\n");
        break;

    case DLL_THREAD_DETACH:
        TRACE("DLL_THREAD_DETACH\n");
        if(local_contexts)
            set_context(NULL);
        break;

    case DLL_PROCESS_DETACH:
        TRACE("DLL_PROCESS_DETACH\n");
        if(openal_handle)
            FreeLibrary(openal_handle);
        TlsFree(TlsThreadPtr);
        if(LogFile != stderr)
            fclose(LogFile);
        LogFile = stderr;
        break;

    default:
        TRACE("UNKNOWN REASON\n");
        break;
    }
    return TRUE;
}

#ifdef __WINESRC__
/***********************************************************************
 *              DllRegisterServer (DSOUND.@)
 */
HRESULT WINAPI DllRegisterServer(void)
{
    return __wine_register_resources(instance);
}

/***********************************************************************
 *              DllUnregisterServer (DSOUND.@)
 */
HRESULT WINAPI DllUnregisterServer(void)
{
    return __wine_unregister_resources(instance);
}
#endif
