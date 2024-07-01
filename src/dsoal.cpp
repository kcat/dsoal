#define INITGUID

#include "dsoal.h"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <tuple>

#include <dsound.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "capture.h"
#include "comhelpers.h"
#include "comptr.h"
#include "dsoal_global.h"
#include "dsoundoal.h"
#include "enumerate.h"
#include "factory.h"
#include "fullduplex.h"
#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

std::mutex gDeviceListMutex;
std::deque<GUID> gPlaybackDevices;
std::deque<GUID> gCaptureDevices;

HMODULE gOpenalHandle{};


template<typename T>
bool load_function(T &func, const char *name)
{
    func = std::bit_cast<T>(GetProcAddress(gOpenalHandle, name));
    if(!func) UNLIKELY
    {
        ERR("load_function Couldn't lookup %s in %ls\n", name, std::data(aldriver_name));
        return false;
    }
    return true;
}

template<typename T>
void load_alcfunction(T &func, const char *name)
{ func = std::bit_cast<T>(alcGetProcAddress(nullptr, name)); }

bool load_openal()
{
    gOpenalHandle = LoadLibraryW(std::data(aldriver_name));
    if(!gOpenalHandle)
    {
        ERR("load_openal Couldn't load %ls: %lu\n", std::data(aldriver_name), GetLastError());
        return false;
    }

    bool ok{true};
#define LOAD_FUNCPTR(f) ok &= load_function(p##f, #f)

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

    if(!ok)
    {
        WARN("load_openal Unloading %ls\n", std::data(aldriver_name));
        if(gOpenalHandle)
            FreeLibrary(gOpenalHandle);
        gOpenalHandle = nullptr;
        return false;
    }

    TRACE("load_openal Loaded %ls\n", std::data(aldriver_name));

    if(!alcIsExtensionPresent(nullptr, "ALC_EXT_thread_local_context"))
    {
        ERR("load_openal Required ALC_EXT_thread_local_context not supported in %ls\n",
            std::data(aldriver_name));
        if(gOpenalHandle)
            FreeLibrary(gOpenalHandle);
        gOpenalHandle = nullptr;
        return false;
    }

#define LOAD_FUNCPTR(f) load_alcfunction(p##f, #f)
    LOAD_FUNCPTR(alcSetThreadContext);
    LOAD_FUNCPTR(alcGetThreadContext);
    LOAD_FUNCPTR(EAXSet);
    LOAD_FUNCPTR(EAXGet);
    LOAD_FUNCPTR(alBufferDataStatic);
    LOAD_FUNCPTR(alBufferStorageSOFT);
    LOAD_FUNCPTR(alMapBufferSOFT);
    LOAD_FUNCPTR(alUnmapBufferSOFT);
    LOAD_FUNCPTR(alFlushMappedBufferSOFT);
#undef LOAD_FUNCPTR

    return true;
}

} // namespace

void SetALContext(ALCcontext *context)
{
    if(context == alcGetThreadContext()) LIKELY
        return;
    if(!alcSetThreadContext(context)) UNLIKELY
        ERR("SetALContext Failed to set context %p!\n", voidp{context});
}


ComPtr<IMMDevice> GetMMDevice(ComWrapper&, EDataFlow flow, const GUID &id)
{
    ComPtr<IMMDeviceEnumerator> devenum;
    HRESULT hr{CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator, ds::out_ptr(devenum))};
    if(FAILED(hr))
    {
        ERR("GetMMDevice CoCreateInstance failed: %08lx\n", hr);
        return {};
    }

    ComPtr<IMMDeviceCollection> coll;
    hr = devenum->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, ds::out_ptr(coll));
    if(FAILED(hr))
    {
        WARN("GetMMDevice IMMDeviceEnumerator::EnumAudioEndpoints failed: %08lx\n", hr);
        return {};
    }

    UINT count{};
    hr = coll->GetCount(&count);
    if(FAILED(hr))
    {
        WARN("GetMMDevice IMMDeviceCollection::GetCount failed: %08lx\n", hr);
        return {};
    }

    for(UINT i{0};i < count;++i)
    {
        ComPtr<IMMDevice> device;
        hr = coll->Item(i, ds::out_ptr(device));
        if(FAILED(hr))
        {
            WARN("GetMMDevice IMMDeviceCollection::Item failed: %08lx\n", hr);
            continue;
        }

        ComPtr<IPropertyStore> ps;
        hr = device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps));
        if(FAILED(hr))
        {
            WARN("GetMMDevice IMMDevice::OpenPropertyStore failed: %08lx\n", hr);
            continue;
        }

        PropVariant pv;
        hr = ps->GetValue(PKEY_AudioEndpoint_GUID, pv.get());
        if(FAILED(hr) || pv.type() != VT_LPWSTR)
        {
            WARN("GetMMDevice IPropertyStore::GetValue(GUID) failed: %08lx\n", hr);
            continue;
        }

        GUID devid{};
        CLSIDFromString(pv.value<const WCHAR*>(), &devid);
        if(id == devid) return device;
    }

    return {};
}


HRESULT WINAPI GetDeviceID(const GUID &guidSrc, GUID &guidDst) noexcept
{
    ERole role{};
    EDataFlow flow{eRender};
    if(DSDEVID_DefaultPlayback == guidSrc)
        role = eMultimedia;
    else if(DSDEVID_DefaultVoicePlayback == guidSrc)
        role = eCommunications;
    else
    {
        flow = eCapture;
        if(DSDEVID_DefaultCapture == guidSrc)
            role = eMultimedia;
        else if(DSDEVID_DefaultVoiceCapture == guidSrc)
            role = eCommunications;
        else
        {
            guidDst = guidSrc;
            return DS_OK;
        }
    }

    ComWrapper com;
    ComPtr<IMMDeviceEnumerator> devenum;
    HRESULT hr{CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator, ds::out_ptr(devenum))};
    if(FAILED(hr))
    {
        ERR("GetDeviceID CoCreateInstance failed: %08lx\n", hr);
        return hr;
    }

    ComPtr<IMMDevice> device;
    hr = devenum->GetDefaultAudioEndpoint(flow, role, ds::out_ptr(device));
    if(FAILED(hr))
    {
        WARN("GetDeviceID IMMDeviceEnumerator::GetDefaultAudioEndpoint failed: %08lx\n", hr);
        return DSERR_NODRIVER;
    }

    ComPtr<IPropertyStore> ps;
    hr = device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps));
    if(FAILED(hr))
    {
        WARN("GetDeviceID IMMDevice::OpenPropertyStore failed: %08lx\n", hr);
        return hr;
    }

    PropVariant pv;
    hr = ps->GetValue(PKEY_AudioEndpoint_GUID, pv.get());
    if(FAILED(hr) || pv.type() != VT_LPWSTR)
    {
        WARN("GetDeviceID IPropertyStore::GetValue(GUID) failed: %08lx\n", hr);
        return hr;
    }

    CLSIDFromString(pv.value<const WCHAR*>(), &guidDst);

    return DS_OK;
}


extern "C" {

#ifdef _MSC_VER
const CLSID CLSID_MMDeviceEnumerator{
    0xBCDE0395,
    0xE52F, 0x467C,
    { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E }
};

const IID IID_IMMDeviceEnumerator{
    0xA95664D2,
    0x9614, 0x4F35,
    { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 }
};

#else

const GUID KSDATAFORMAT_SUBTYPE_PCM{
    0x00000001,
    0x0000, 0x0010,
    { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }
};
const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT{
    0x00000003,
    0x0000, 0x0010,
    { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9b, 0x71 }
};
#endif

DSOAL_EXPORT BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD reason, void *reserved)
{
    DEBUG("DllMain (%p, %lu, %p)\n", voidp{hInstDLL}, reason, reserved);

    switch(reason)
    {
    case DLL_PROCESS_ATTACH:
        if(const WCHAR *wstr{_wgetenv(L"DSOAL_LOGFILE")}; wstr && *wstr != 0)
        {
            FILE *f = _wfopen(wstr, L"wt");
            if(!f) ERR("Failed to open log file %ls\n", wstr);
            else gLogFile = f;
        }

        if(const char *str{std::getenv("DSOAL_LOGLEVEL")}; str && *str != 0)
        {
            char *endptr{};
            const auto level = std::strtol(str, &endptr, 0) + 1;
            if(!endptr || *endptr != 0)
                ERR("Invalid log level specified: \"%s\"\n", str);
            else
            {
                if(level < static_cast<long>(LogLevel::Disable))
                    gLogLevel = LogLevel::Disable;
                else if(level > static_cast<long>(LogLevel::Debug))
                    gLogLevel = LogLevel::Debug;
                else
                    gLogLevel = static_cast<LogLevel>(level);
            }
        }

        if(!load_openal())
        {
            if(gLogFile)
                fclose(gLogFile);
            gLogFile = nullptr;
            return FALSE;
        }

        /* Increase refcount on dsound by 1 */
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(hInstDLL), &hInstDLL);
        break;

    case DLL_PROCESS_DETACH:
        if(gLogFile)
            fclose(gLogFile);
        gLogFile = nullptr;
        break;
    }

    return TRUE;
}


HRESULT WINAPI DSOAL_DirectSoundCreate(const GUID *deviceId, IDirectSound **ds, IUnknown *outer) noexcept
{
    TRACE("DirectSoundCreate (%s, %p, %p)\n", GuidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});

    if(!ds)
    {
        WARN("DirectSoundCreate invalid parameter: ppDS == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("DirectSoundCreate invalid parameter: pUnkOuter != NULL\n");
        return DSERR_INVALIDPARAM;
    }

    HRESULT hr{};
    try {
        auto dsobj = DSound8OAL::Create(false);
        hr = dsobj->Initialize(deviceId);
        if(SUCCEEDED(hr))
        {
            *ds = dsobj.release()->as<IDirectSound*>();
            return DS_OK;
        }
    }
    catch(std::bad_alloc &e) {
        ERR("DirectSoundCreate Caught exception: %s\n", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}

HRESULT WINAPI DSOAL_DirectSoundCreate8(const GUID *deviceId, IDirectSound8 **ds, IUnknown *outer) noexcept
{
    TRACE("DirectSoundCreate8 (%s, %p, %p)\n", DevidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});

    if(!ds)
    {
        WARN("DirectSoundCreate8 invalid parameter: ppDS == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("DirectSoundCreate8 invalid parameter: pUnkOuter != NULL\n");
        return DSERR_INVALIDPARAM;
    }

    HRESULT hr{};
    try {
        auto dsobj = DSound8OAL::Create(true);
        hr = dsobj->Initialize(deviceId);
        if(SUCCEEDED(hr))
        {
            *ds = dsobj.release()->as<IDirectSound8*>();
            return DS_OK;
        }
    }
    catch(std::bad_alloc &e) {
        ERR("DirectSoundCreate8 Caught exception: %s\n", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureCreate(const GUID *deviceId, IDirectSoundCapture **ds,
    IUnknown *outer) noexcept
{
    TRACE("DirectSoundCaptureCreate (%s, %p, %p)\n", DevidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});

    if(!ds)
    {
        WARN("DirectSoundCaptureCreate invalid parameter: ppDS == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("DirectSoundCaptureCreate invalid parameter: pUnkOuter != NULL\n");
        return DSERR_INVALIDPARAM;
    }

    HRESULT hr{};
    try {
        auto dsobj = DSCapture::Create(false);
        hr = dsobj->Initialize(deviceId);
        if(SUCCEEDED(hr))
        {
            *ds = dsobj.release()->as<IDirectSoundCapture*>();
            return DS_OK;
        }
    }
    catch(std::bad_alloc &e) {
        ERR("DirectSoundCaptureCreate Caught exception: %s\n", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureCreate8(const GUID *deviceId, IDirectSoundCapture8 **ds,
    IUnknown *outer) noexcept
{
    TRACE("DirectSoundCaptureCreate8 (%s, %p, %p)\n", DevidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});

    if(!ds)
    {
        WARN("DirectSoundCaptureCreate8 invalid parameter: ppDS == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("DirectSoundCaptureCreate8 invalid parameter: pUnkOuter != NULL\n");
        return DSERR_INVALIDPARAM;
    }

    HRESULT hr{};
    try {
        auto dsobj = DSCapture::Create(true);
        hr = dsobj->Initialize(deviceId);
        if(SUCCEEDED(hr))
        {
            *ds = dsobj.release()->as<IDirectSoundCapture8*>();
            return DS_OK;
        }
    }
    catch(std::bad_alloc &e) {
        ERR("DirectSoundCaptureCreate8 Caught exception: %s\n", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}

HRESULT WINAPI DSOAL_DirectSoundFullDuplexCreate(const GUID *captureDevice,
    const GUID *renderDevice, const DSCBUFFERDESC *captureBufferDesc,
    const DSBUFFERDESC *renderBufferDesc, HWND hWnd, DWORD coopLevel,
    IDirectSoundFullDuplex **fullDuplex, IDirectSoundCaptureBuffer8 **captureBuffer8,
    IDirectSoundBuffer8 **renderBuffer8, IUnknown *outer) noexcept
{
    TRACE("DirectSoundFullDuplexCreate (%s, %s, %p, %p, %p, %lu, %p, %p, %p, %p)\n",
        DevidPrinter{captureDevice}.c_str(), DevidPrinter{renderDevice}.c_str(),
        cvoidp{captureBufferDesc}, cvoidp{renderBufferDesc}, voidp{hWnd}, coopLevel,
        voidp{fullDuplex}, voidp{captureBuffer8}, voidp{renderBuffer8}, voidp{outer});

    if(renderBuffer8) *renderBuffer8 = nullptr;
    if(captureBuffer8) *captureBuffer8 = nullptr;
    if(!fullDuplex)
    {
        WARN("DirectSoundFullDuplexCreate invalid out parameter: %p\n", voidp{fullDuplex});
        return DSERR_INVALIDPARAM;
    }
    *fullDuplex = nullptr;

    if(outer)
    {
        WARN("DirectSoundFullDuplexCreate invalid parameter: pUnkOuter != NULL\n");
        return DSERR_INVALIDPARAM;
    }

    HRESULT hr{};
    try {
        auto dsobj = DSFullDuplex::Create();
        hr = dsobj->Initialize(captureDevice, renderDevice, captureBufferDesc, renderBufferDesc,
            hWnd, coopLevel, captureBuffer8, renderBuffer8);
        if(SUCCEEDED(hr))
        {
            *fullDuplex = dsobj.release()->as<IDirectSoundFullDuplex*>();
            return DS_OK;
        }
    }
    catch(std::bad_alloc &e) {
        ERR("DirectSoundFullDuplexCreate Caught exception: %s\n", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}


HRESULT WINAPI DSOAL_DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, void *userPtr) noexcept
{
    TRACE("DirectSoundEnumerateA (%p, %p)\n", reinterpret_cast<void*>(callback), userPtr);

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *drvname, const WCHAR *devname)
    {
        const auto dlen = WideCharToMultiByte(CP_ACP, 0, drvname, -1, nullptr, 0, nullptr, nullptr);
        const auto mlen = WideCharToMultiByte(CP_ACP, 0, devname, -1, nullptr, 0, nullptr, nullptr);
        if(dlen < 0 || mlen < 0) return false;

        auto descA = std::make_unique<char[]>(static_cast<size_t>(dlen+mlen)+2);
        if(!descA) return false;
        char *modA = descA.get() + dlen+1;

        WideCharToMultiByte(CP_ACP, 0, drvname, -1, descA.get(), dlen, nullptr, nullptr);
        WideCharToMultiByte(CP_ACP, 0, devname, -1, modA, mlen, nullptr, nullptr);

        return callback(guid, descA.get(), modA, userPtr) != FALSE;
    };

    std::lock_guard listlock{gDeviceListMutex};
    return enumerate_mmdev(eRender, gPlaybackDevices, do_enum);
}
HRESULT WINAPI DSOAL_DirectSoundEnumerateW(LPDSENUMCALLBACKW callback, void *userPtr) noexcept
{
    TRACE("DirectSoundEnumerateW (%p, %p)\n", reinterpret_cast<void*>(callback), userPtr);

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *drvname, const WCHAR *devname)
    { return callback(guid, drvname, devname, userPtr) != FALSE; };

    std::lock_guard listlock{gDeviceListMutex};
    return enumerate_mmdev(eRender, gPlaybackDevices, do_enum);
}

HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateA(LPDSENUMCALLBACKA callback, void *userPtr) noexcept
{
    TRACE("DirectSoundCaptureEnumerateA (%p, %p)\n", reinterpret_cast<void*>(callback), userPtr);

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *drvname, const WCHAR *devname)
    {
        const auto dlen = WideCharToMultiByte(CP_ACP, 0, drvname, -1, nullptr, 0, nullptr, nullptr);
        const auto mlen = WideCharToMultiByte(CP_ACP, 0, devname, -1, nullptr, 0, nullptr, nullptr);
        if(dlen < 0 || mlen < 0) return false;

        auto descA = std::make_unique<char[]>(static_cast<size_t>(dlen+mlen)+2);
        if(!descA) return false;
        char *modA = descA.get() + dlen+1;

        WideCharToMultiByte(CP_ACP, 0, drvname, -1, descA.get(), dlen, nullptr, nullptr);
        WideCharToMultiByte(CP_ACP, 0, devname, -1, modA, mlen, nullptr, nullptr);

        return callback(guid, descA.get(), modA, userPtr) != FALSE;
    };

    std::lock_guard listlock{gDeviceListMutex};
    return enumerate_mmdev(eCapture, gCaptureDevices, do_enum);
}
HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW callback, void *userPtr) noexcept
{
    TRACE("DirectSoundCaptureEnumerateW (%p, %p)\n", reinterpret_cast<void*>(callback), userPtr);

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *drvname, const WCHAR *devname)
    { return callback(guid, drvname, devname, userPtr) != FALSE; };

    std::lock_guard listlock{gDeviceListMutex};
    return enumerate_mmdev(eCapture, gCaptureDevices, do_enum);
}


HRESULT WINAPI DSOAL_DllCanUnloadNow() noexcept
{
    TRACE("DllCanUnloadNow\n");
    return S_FALSE;
}

HRESULT WINAPI DSOAL_DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) noexcept
{
    TRACE("DllGetClassObject (%s, %s, %p)\n", ClsidPrinter{rclsid}.c_str(),
        IidPrinter{riid}.c_str(), voidp{ppv});

    if(!ppv)
    {
        WARN("DllGetClassObject NULL out pointer\n");
        return E_INVALIDARG;
    }
    *ppv = nullptr;

    return Factory::GetFactory(rclsid, riid, ppv);
}

HRESULT WINAPI DSOAL_GetDeviceID(const GUID *guidSrc, GUID *guidDst) noexcept
{
    TRACE("GetDeviceID (%s, %p)\n", DevidPrinter{guidSrc}.c_str(), voidp{guidDst});

    if(!guidSrc || !guidDst)
        return DSERR_INVALIDPARAM;

    return GetDeviceID(*guidSrc, *guidDst);
}

} // extern "C"
