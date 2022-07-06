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
#define INITGUID
#include <stdarg.h>
#include <string.h>

#include <windows.h>
#include <dsound.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <devpropdef.h>

#include "dsound_private.h"
#include "eax-presets.h"

#ifndef DECLSPEC_EXPORT
#ifdef _WIN32
#define DECLSPEC_EXPORT __declspec(dllexport)
#else
#define DECLSPEC_EXPORT
#endif
#endif


int LogLevel = 1;
FILE *LogFile;


typedef struct DeviceList {
    GUID *Guids;
    size_t Count;
} DeviceList;
static DeviceList PlaybackDevices = { NULL, 0 };
static DeviceList CaptureDevices = { NULL, 0 };

const WCHAR aldriver_name[] = L"dsoal-aldrv.dll";


const EAXREVERBPROPERTIES EnvironmentDefaults[EAX_ENVIRONMENT_UNDEFINED] = {
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

CRITICAL_SECTION openal_crst;

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

LPEAXSET pEAXSet = NULL;
LPEAXGET pEAXGet = NULL;
LPALDEFERUPDATESSOFT palDeferUpdatesSOFT = NULL;
LPALPROCESSUPDATESSOFT palProcessUpdatesSOFT = NULL;
LPALBUFFERSTORAGESOFT palBufferStorageSOFT = NULL;
LPALMAPBUFFERSOFT palMapBufferSOFT = NULL;
LPALUNMAPBUFFERSOFT palUnmapBufferSOFT = NULL;
LPALFLUSHMAPPEDBUFFERSOFT palFlushMappedBufferSOFT = NULL;

LPALCMAKECONTEXTCURRENT set_context;
LPALCGETCURRENTCONTEXT get_context;
BOOL local_contexts;


static void AL_APIENTRY wrap_DeferUpdates(void)
{ alcSuspendContext(alcGetCurrentContext()); }
static void AL_APIENTRY wrap_ProcessUpdates(void)
{ alcProcessContext(alcGetCurrentContext()); }

static void EnterALSectionTLS(ALCcontext *ctx);
static void LeaveALSectionTLS(void);
static void EnterALSectionGlob(ALCcontext *ctx);
static void LeaveALSectionGlob(void);

DWORD TlsThreadPtr;
void (*EnterALSection)(ALCcontext *ctx) = EnterALSectionGlob;
void (*LeaveALSection)(void) = LeaveALSectionGlob;


static BOOL load_libopenal(void)
{
    BOOL failed = FALSE;
    const char *str;

    str = getenv("DSOAL_LOGLEVEL");
    if(str && *str)
        LogLevel = atoi(str);

    openal_handle = LoadLibraryW(aldriver_name);
    if(!openal_handle)
    {
        ERR("Couldn't load %ls: %lu\n", aldriver_name, GetLastError());
        return FALSE;
    }

#define LOAD_FUNCPTR(f) do {                                     \
    union { void *ptr; FARPROC *proc; } func = { &p##f };        \
    if((*func.proc = GetProcAddress(openal_handle, #f)) == NULL) \
    {                                                            \
        ERR("Couldn't lookup %s in %ls\n", #f, aldriver_name);   \
        failed = TRUE;                                           \
    }                                                            \
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
        WARN("Unloading %ls\n", aldriver_name);
        if (openal_handle != NULL)
            FreeLibrary(openal_handle);
        openal_handle = NULL;
        return FALSE;
    }

    openal_loaded = 1;
    TRACE("Loaded %ls\n", aldriver_name);

#define LOAD_FUNCPTR(f) p##f = alcGetProcAddress(NULL, #f)
    LOAD_FUNCPTR(EAXSet);
    LOAD_FUNCPTR(EAXGet);
    LOAD_FUNCPTR(alDeferUpdatesSOFT);
    LOAD_FUNCPTR(alProcessUpdatesSOFT);
    LOAD_FUNCPTR(alBufferStorageSOFT);
    LOAD_FUNCPTR(alMapBufferSOFT);
    LOAD_FUNCPTR(alUnmapBufferSOFT);
    LOAD_FUNCPTR(alFlushMappedBufferSOFT);
#undef LOAD_FUNCPTR
    if(!palDeferUpdatesSOFT || !palProcessUpdatesSOFT)
    {
        palDeferUpdatesSOFT = wrap_DeferUpdates;
        palProcessUpdatesSOFT = wrap_ProcessUpdates;
    }

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


static const char *get_device_id(LPCGUID pGuid)
{
    if(IsEqualGUID(&DSDEVID_DefaultPlayback, pGuid))
        return "DSDEVID_DefaultPlayback";
    if(IsEqualGUID(&DSDEVID_DefaultVoicePlayback, pGuid))
        return "DSDEVID_DefaultVoicePlayback";
    if(IsEqualGUID(&DSDEVID_DefaultCapture, pGuid))
        return "DSDEVID_DefaultCapture";
    if(IsEqualGUID(&DSDEVID_DefaultVoiceCapture, pGuid))
        return "DSDEVID_DefaultVoiceCapture";
    return debugstr_guid(pGuid);
}

#ifdef _MSC_VER
const CLSID CLSID_MMDeviceEnumerator = {
    0xBCDE0395,
    0xE52F, 0x467C,
	{ 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E }
};

const IID IID_IMMDeviceEnumerator = {
    0xA95664D2,
    0x9614, 0x4F35,
    { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 }
};
#endif

static HRESULT get_mmdevenum(IMMDeviceEnumerator **devenum)
{
    HRESULT hr, init_hr;

    init_hr = CoInitialize(NULL);

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL,
            CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, (void**)devenum);
    if(FAILED(hr))
    {
        if(SUCCEEDED(init_hr))
            CoUninitialize();
        *devenum = NULL;
        ERR("CoCreateInstance failed: %08lx\n", hr);
        return hr;
    }

    return init_hr;
}

static void release_mmdevenum(IMMDeviceEnumerator *devenum, HRESULT init_hr)
{
    IMMDeviceEnumerator_Release(devenum);
    if(SUCCEEDED(init_hr))
        CoUninitialize();
}

static HRESULT get_mmdevice_guid(IMMDevice *device, IPropertyStore *ps, GUID *guid)
{
    PROPVARIANT pv;
    HRESULT hr;

    if(ps)
        IPropertyStore_AddRef(ps);
    else
    {
        hr = IMMDevice_OpenPropertyStore(device, STGM_READ, &ps);
        if(FAILED(hr))
        {
            WARN("OpenPropertyStore failed: %08lx\n", hr);
            return hr;
        }
    }

    PropVariantInit(&pv);

    hr = IPropertyStore_GetValue(ps, &PKEY_AudioEndpoint_GUID, &pv);
    if(FAILED(hr))
    {
        IPropertyStore_Release(ps);
        WARN("GetValue(GUID) failed: %08lx\n", hr);
        return hr;
    }

    CLSIDFromString(pv.pwszVal, guid);

    PropVariantClear(&pv);
    IPropertyStore_Release(ps);

    return S_OK;
}


static BOOL send_device(IMMDevice *device, EDataFlow flow, DeviceList *devlist, PRVTENUMCALLBACK cb, void *user)
{
    IPropertyStore *ps;
    PROPVARIANT pv;
    BOOL keep_going;
    size_t dev_count;
    HRESULT hr;
    GUID guid;

    PropVariantInit(&pv);

    hr = IMMDevice_OpenPropertyStore(device, STGM_READ, &ps);
    if(FAILED(hr))
    {
        WARN("OpenPropertyStore failed: %08lx\n", hr);
        return TRUE;
    }

    hr = get_mmdevice_guid(device, ps, &guid);
    if(FAILED(hr) || (devlist->Count > 0 && IsEqualGUID(&devlist->Guids[0], &guid)))
    {
        IPropertyStore_Release(ps);
        return TRUE;
    }

    hr = IPropertyStore_GetValue(ps, (const PROPERTYKEY*)&DEVPKEY_Device_FriendlyName, &pv);
    if(FAILED(hr))
    {
        IPropertyStore_Release(ps);
        WARN("GetValue(FriendlyName) failed: %08lx\n", hr);
        return TRUE;
    }

    dev_count = devlist->Count++;
    devlist->Guids[dev_count] = guid;

    keep_going = FALSE;
    if(cb)
    {
        TRACE("Calling back with %s - %ls\n", debugstr_guid(&devlist->Guids[dev_count]),
              pv.pwszVal);
        keep_going = cb(flow, &devlist->Guids[dev_count], pv.pwszVal, aldriver_name, user);
    }

    PropVariantClear(&pv);
    IPropertyStore_Release(ps);

    return keep_going;
}

HRESULT get_mmdevice(EDataFlow flow, const GUID *tgt, IMMDevice **device)
{
    IMMDeviceEnumerator *devenum;
    IMMDeviceCollection *coll;
    UINT count, i;
    HRESULT hr, init_hr;

    *device = NULL;

    init_hr = get_mmdevenum(&devenum);
    if(!devenum) return init_hr;

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(devenum, flow, DEVICE_STATE_ACTIVE, &coll);
    if(FAILED(hr))
    {
        WARN("EnumAudioEndpoints failed: %08lx\n", hr);
        release_mmdevenum(devenum, init_hr);
        return hr;
    }

    hr = IMMDeviceCollection_GetCount(coll, &count);
    if(FAILED(hr))
    {
        IMMDeviceCollection_Release(coll);
        release_mmdevenum(devenum, init_hr);
        WARN("GetCount failed: %08lx\n", hr);
        return hr;
    }

    for(i = 0; i < count;++i)
    {
        GUID guid;

        hr = IMMDeviceCollection_Item(coll, i, device);
        if(FAILED(hr)) continue;

        hr = get_mmdevice_guid(*device, NULL, &guid);
        if(SUCCEEDED(hr) && IsEqualGUID(&guid, tgt))
        {
            IMMDeviceCollection_Release(coll);
            IMMDeviceEnumerator_Release(devenum);
            return init_hr;
        }

        IMMDevice_Release(*device);
        *device = NULL;
    }

    WARN("No device with GUID %s found!\n", debugstr_guid(tgt));

    IMMDeviceCollection_Release(coll);
    release_mmdevenum(devenum, init_hr);

    return DSERR_INVALIDPARAM;
}

void release_mmdevice(IMMDevice *device, HRESULT init_hr)
{
    IMMDevice_Release(device);
    if(SUCCEEDED(init_hr))
        CoUninitialize();
}

/* S_FALSE means the callback returned FALSE at some point
 * S_OK means the callback always returned TRUE */
HRESULT enumerate_mmdevices(EDataFlow flow, PRVTENUMCALLBACK cb, void *user)
{
    static const WCHAR primary_desc[] = L"Primary Sound Driver";

    IMMDeviceEnumerator *devenum;
    IMMDeviceCollection *coll;
    IMMDevice *device;
    DeviceList *devlist;
    UINT count, i;
    BOOL keep_going;
    HRESULT hr, init_hr;

    init_hr = get_mmdevenum(&devenum);
    if(!devenum) return init_hr;

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(devenum, flow, DEVICE_STATE_ACTIVE, &coll);
    if(FAILED(hr))
    {
        release_mmdevenum(devenum, init_hr);
        WARN("EnumAudioEndpoints failed: %08lx\n", hr);
        return DS_OK;
    }

    hr = IMMDeviceCollection_GetCount(coll, &count);
    if(FAILED(hr))
    {
        IMMDeviceCollection_Release(coll);
        release_mmdevenum(devenum, init_hr);
        WARN("GetCount failed: %08lx\n", hr);
        return DS_OK;
    }

    if(count == 0)
    {
        IMMDeviceCollection_Release(coll);
        release_mmdevenum(devenum, init_hr);
        return DS_OK;
    }

    devlist = (flow==eCapture) ? &CaptureDevices : &PlaybackDevices;

    HeapFree(GetProcessHeap(), 0, devlist->Guids);
    devlist->Count = 0;
    devlist->Guids = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                               sizeof(devlist->Guids[0])*count);

    TRACE("Calling back with NULL (%ls)\n", primary_desc);
    keep_going = cb(flow, NULL, primary_desc, L"", user);

    /* always send the default device first */
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(devenum, flow, eMultimedia, &device);
    if(SUCCEEDED(hr))
    {
        if(!keep_going) cb = NULL;
        keep_going = send_device(device, flow, devlist, cb, user);
        IMMDevice_Release(device);
    }

    for(i = 0;i < count;++i)
    {
        if(!keep_going)
            cb = NULL;

        hr = IMMDeviceCollection_Item(coll, i, &device);
        if(FAILED(hr))
        {
            WARN("Item failed: %08lx\n", hr);
            continue;
        }

        keep_going = send_device(device, flow, devlist, cb, user);

        IMMDevice_Release(device);
    }
    IMMDeviceCollection_Release(coll);

    release_mmdevenum(devenum, init_hr);

    return keep_going ? S_OK : S_FALSE;
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
HRESULT WINAPI DSOAL_GetDeviceID(LPCGUID pGuidSrc, LPGUID pGuidDest)
{
    IMMDeviceEnumerator *devenum;
    HRESULT hr, init_hr;
    IMMDevice *device;
    EDataFlow flow;
    ERole role;

    TRACE("(%s, %p)\n", get_device_id(pGuidSrc), pGuidDest);

    if(!pGuidSrc || !pGuidDest)
        return DSERR_INVALIDPARAM;

    flow = eRender;
    if(IsEqualGUID(&DSDEVID_DefaultPlayback, pGuidSrc))
        role = eMultimedia;
    else if(IsEqualGUID(&DSDEVID_DefaultVoicePlayback, pGuidSrc))
        role = eCommunications;
    else
    {
        flow = eCapture;
        if(IsEqualGUID(&DSDEVID_DefaultCapture, pGuidSrc))
            role = eMultimedia;
        else if(IsEqualGUID(&DSDEVID_DefaultVoiceCapture, pGuidSrc))
            role = eCommunications;
        else
        {
            *pGuidDest = *pGuidSrc;
            return DS_OK;
        }
    }

    init_hr = get_mmdevenum(&devenum);
    if(!devenum) return init_hr;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(devenum, flow, role, &device);
    if(FAILED(hr))
    {
        WARN("GetDefaultAudioEndpoint failed: %08lx\n", hr);
        release_mmdevenum(devenum, init_hr);
        return DSERR_NODRIVER;
    }

    hr = get_mmdevice_guid(device, NULL, pGuidDest);
    IMMDevice_Release(device);

    release_mmdevenum(devenum, init_hr);

    return hr;
}


struct morecontextW {
    LPDSENUMCALLBACKW callW;
    LPVOID data;
};

static BOOL CALLBACK w_callback(EDataFlow flow, LPGUID guid, LPCWSTR descW, LPCWSTR modW, LPVOID data)
{
    struct morecontextW *context = data;
    (void)flow;

    return context->callW(guid, descW, modW, context->data);
}

struct morecontextA {
    LPDSENUMCALLBACKA callA;
    LPVOID data;
};

static BOOL CALLBACK w_to_a_callback(EDataFlow flow, LPGUID guid, LPCWSTR descW, LPCWSTR modW, LPVOID data)
{
    struct morecontextA *context = data;
    char *descA, *modA;
    int dlen, mlen;
    BOOL ret;
    (void)flow;

    dlen = WideCharToMultiByte(CP_ACP, 0, descW, -1, NULL, 0, NULL, NULL);
    mlen = WideCharToMultiByte(CP_ACP, 0, modW, -1, NULL, 0, NULL, NULL);
    if(dlen < 0 || mlen < 0) return FALSE;

    descA = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dlen+mlen+2);
    if(!descA) return FALSE;
    modA = descA + dlen+1;

    WideCharToMultiByte(CP_ACP, 0, descW, -1, descA, dlen, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, modW, -1, modA, mlen, NULL, NULL);

    ret = context->callA(guid, descA, modA, context->data);

    HeapFree(GetProcessHeap(), 0, descA);
    return ret;
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
HRESULT WINAPI DSOAL_DirectSoundEnumerateA(
    LPDSENUMCALLBACKA lpDSEnumCallback,
    LPVOID lpContext)
{
    struct morecontextA ctx;
    HRESULT hr;

    TRACE("(%p, %p)\n", lpDSEnumCallback, lpContext);

    if(lpDSEnumCallback == NULL)
    {
        WARN("invalid parameter: lpDSEnumCallback == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    ctx.callA = lpDSEnumCallback;
    ctx.data = lpContext;

    hr = enumerate_mmdevices(eRender, w_to_a_callback, &ctx);
    return SUCCEEDED(hr) ? DS_OK : hr;
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
HRESULT WINAPI DSOAL_DirectSoundEnumerateW(
    LPDSENUMCALLBACKW lpDSEnumCallback,
    LPVOID lpContext )
{
    struct morecontextW ctx;
    HRESULT hr;

    TRACE("(%p, %p)\n", lpDSEnumCallback, lpContext);

    if(lpDSEnumCallback == NULL)
    {
        WARN("invalid parameter: lpDSEnumCallback == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    ctx.callW = lpDSEnumCallback;
    ctx.data = lpContext;

    hr = enumerate_mmdevices(eRender, w_callback, &ctx);
    return SUCCEEDED(hr) ? DS_OK : hr;
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
HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateA(
    LPDSENUMCALLBACKA lpDSEnumCallback,
    LPVOID lpContext)
{
    struct morecontextA ctx;
    HRESULT hr;

    TRACE("(%p, %p)\n", lpDSEnumCallback, lpContext);

    if(lpDSEnumCallback == NULL)
    {
        WARN("invalid parameter: lpDSEnumCallback == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    ctx.callA = lpDSEnumCallback;
    ctx.data = lpContext;

    hr = enumerate_mmdevices(eCapture, w_to_a_callback, &ctx);
    return SUCCEEDED(hr) ? DS_OK : hr;
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
HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateW(
    LPDSENUMCALLBACKW lpDSEnumCallback,
    LPVOID lpContext)
{
    struct morecontextW ctx;
    HRESULT hr;

    TRACE("(%p, %p)\n", lpDSEnumCallback, lpContext);

    if(lpDSEnumCallback == NULL)
    {
        WARN("invalid parameter: lpDSEnumCallback == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    ctx.callW = lpDSEnumCallback;
    ctx.data = lpContext;

    hr = enumerate_mmdevices(eCapture, w_callback, &ctx);
    return SUCCEEDED(hr) ? DS_OK : hr;
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
HRESULT WINAPI
DSOAL_DirectSoundCreate(LPCGUID lpcGUID, IDirectSound **ppDS, IUnknown *pUnkOuter)
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
HRESULT WINAPI
DSOAL_DirectSoundCreate8(LPCGUID lpcGUID, IDirectSound8 **ppDS, IUnknown *pUnkOuter)
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
HRESULT WINAPI
DSOAL_DirectSoundCaptureCreate(LPCGUID lpcGUID, IDirectSoundCapture **ppDSC, IUnknown *pUnkOuter)
{
    HRESULT hr;
    void *pDSC;

    TRACE("(%s, %p, %p)\n", debugstr_guid(lpcGUID), ppDSC, pUnkOuter);

    if(pUnkOuter)
    {
        WARN("invalid parameter: pUnkOuter != NULL\n");
        return DSERR_NOAGGREGATION;
    }

    if(ppDSC == NULL)
    {
        WARN("invalid parameter: ppDSC == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ppDSC = NULL;

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
HRESULT WINAPI
DSOAL_DirectSoundCaptureCreate8(LPCGUID lpcGUID, IDirectSoundCapture8 **ppDSC8, IUnknown *pUnkOuter)
{
    HRESULT hr;
    void *pDSC8;

    TRACE("(%s, %p, %p)\n", debugstr_guid(lpcGUID), ppDSC8, pUnkOuter);

    if(pUnkOuter)
    {
        WARN("invalid parameter: pUnkOuter != NULL\n");
        return DSERR_NOAGGREGATION;
    }

    if(ppDSC8 == NULL)
    {
        WARN("invalid parameter: ppDSC8 == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ppDSC8 = NULL;

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
    TRACE("(%p) ref %lu\n", iface, ref);
    return ref;
}

static ULONG WINAPI DSCF_Release(LPCLASSFACTORY iface)
{
    IClassFactoryImpl *This = impl_from_IClassFactory(iface);
    ULONG ref = InterlockedDecrement(&(This->ref));
    TRACE("(%p) ref %lu\n", iface, ref);
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
HRESULT WINAPI DSOAL_DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
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
            *ppv = &DSOUND_CF[i].IClassFactory_iface;
            return S_OK;
        }
        i++;
    }

    WARN("No class found for %s\n", debugstr_guid(rclsid));
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
HRESULT WINAPI DSOAL_DllCanUnloadNow(void)
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

        if(!load_libopenal())
            return FALSE;
        TlsThreadPtr = TlsAlloc();
        InitializeCriticalSection(&openal_crst);
        /* Increase refcount on dsound by 1 */
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)hInstDLL, &hInstDLL);
        break;

    case DLL_THREAD_ATTACH:
        break;

    case DLL_THREAD_DETACH:
        break;

    case DLL_PROCESS_DETACH:
        HeapFree(GetProcessHeap(), 0, PlaybackDevices.Guids);
        PlaybackDevices.Guids = NULL;
        PlaybackDevices.Count = 0;
        HeapFree(GetProcessHeap(), 0, CaptureDevices.Guids);
        CaptureDevices.Guids = NULL;
        CaptureDevices.Count = 0;

        if(openal_handle)
            FreeLibrary(openal_handle);
        TlsFree(TlsThreadPtr);
        DeleteCriticalSection(&openal_crst);
        if(LogFile != stderr)
            fclose(LogFile);
        LogFile = stderr;
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
