#define INITGUID

#include "dsoal.h"
#include "dsoal_global.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <devpkey.h>
#include <dsound.h>
#include <mmdeviceapi.h>
#include <mutex>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "comhelpers.h"
#include "comptr.h"
#include "dsoundoal.h"
#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

HMODULE gOpenalHandle{};

constexpr WCHAR aldriver_name[] = L"dsoal-aldrv.dll";

template<typename T>
bool load_function(T &func, const char *name)
{
    const auto ptr = GetProcAddress(gOpenalHandle, name);
    static_assert(sizeof(T) == sizeof(decltype(ptr)));

    std::memcpy(&func, &ptr, sizeof(func));
    if(!func) UNLIKELY
    {
        ERR("load_function Couldn't lookup %s in %ls\n", name, aldriver_name);
        return false;
    }
    return true;
}

template<typename T>
void load_alcfunction(T &func, const char *name)
{
    const auto ptr = alcGetProcAddress(nullptr, name);
    static_assert(sizeof(T) == sizeof(decltype(ptr)));

    std::memcpy(&func, &ptr, sizeof(func));
}

bool load_openal()
{
    gOpenalHandle = LoadLibraryW(aldriver_name);
    if(!gOpenalHandle)
    {
        ERR("load_openal Couldn't load %ls: %lu\n", aldriver_name, GetLastError());
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
        WARN("load_openal Unloading %ls\n", aldriver_name);
        if(gOpenalHandle)
            FreeLibrary(gOpenalHandle);
        gOpenalHandle = nullptr;
        return false;
    }

    TRACE("load_openal Loaded %ls\n", aldriver_name);

    if(!alcIsExtensionPresent(nullptr, "ALC_EXT_thread_local_context"))
    {
        ERR("load_openal Required ALC_EXT_thread_local_context not supported in %ls\n", aldriver_name);
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
    LOAD_FUNCPTR(alDeferUpdatesSOFT);
    LOAD_FUNCPTR(alProcessUpdatesSOFT);
    LOAD_FUNCPTR(alBufferStorageSOFT);
    LOAD_FUNCPTR(alMapBufferSOFT);
    LOAD_FUNCPTR(alUnmapBufferSOFT);
    LOAD_FUNCPTR(alFlushMappedBufferSOFT);
#undef LOAD_FUNCPTR

    return true;
}

} // namespace


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
    if(FAILED(hr) || pv->vt != VT_LPWSTR)
    {
        WARN("GetDeviceID IPropertyStore::GetValue(GUID) failed: %08lx\n", hr);
        return hr;
    }

    CLSIDFromString(pv->pwszVal, &guidDst);

    return DS_OK;
}


extern "C" {

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

DSOAL_EXPORT BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD reason, void *reserved)
{
    DEBUG("DllMain (%p, %lu, %p)\n", voidp{hInstDLL}, reason, reserved);

    switch(reason)
    {
    case DLL_PROCESS_ATTACH:
        if(const WCHAR *wstr{_wgetenv(L"DSOAL_LOGFILE")}; wstr && wstr[0] != 0)
        {
            FILE *f = _wfopen(wstr, L"wt");
            if(!f) ERR("Failed to open log file %ls\n", wstr);
            else gLogFile = f;
        }

        if(const char *str{std::getenv("DSOAL_LOGLEVEL")}; str && str[0] != 0)
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
            if(gLogFile != stderr)
                fclose(gLogFile);
            gLogFile = stderr;
            return FALSE;
        }

        /* Increase refcount on dsound by 1 */
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(hInstDLL), &hInstDLL);
        break;

    case DLL_PROCESS_DETACH:
        if(gLogFile != stderr)
            fclose(gLogFile);
        gLogFile = stderr;
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
    *ds = NULL;

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
    catch(std::bad_alloc&) {
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}

HRESULT WINAPI DSOAL_DirectSoundCreate8(const GUID *deviceId, IDirectSound8 **ds, IUnknown *outer) noexcept
{
    TRACE("DirectSoundCreate8 (%s, %p, %p)\n", GuidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});

    if(!ds)
    {
        WARN("DirectSoundCreate8 invalid parameter: ppDS == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ds = NULL;

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
    catch(std::bad_alloc&) {
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureCreate(const GUID *deviceId, IDirectSoundCapture **ds,
    IUnknown *outer) noexcept
{
    TRACE("DirectSoundCaptureCreate (%s, %p, %p)\n", GuidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureCreate8(const GUID *deviceId, IDirectSoundCapture8 **ds,
    IUnknown *outer) noexcept
{
    TRACE("DirectSoundCaptureCreate8 (%s, %p, %p)\n", GuidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundFullDuplexCreate(const GUID *captureDevice,
    const GUID *renderDevice, const DSCBUFFERDESC *captureBufferDesc,
    const DSBUFFERDESC *renderBufferDesc, HWND hWnd, DWORD coopLevel,
    IDirectSoundFullDuplex **fullDuplex, IDirectSoundCaptureBuffer8 **captureBuffer8,
    IDirectSoundBuffer8 **renderBuffer8, IUnknown *outer) noexcept
{
    TRACE("DirectSoundFullDuplexCreate (%s, %s, %p, %p, %p, %lu, %p, %p, %p, %p)\n",
        GuidPrinter{captureDevice}.c_str(), GuidPrinter{renderDevice}.c_str(),
        cvoidp{captureBufferDesc}, cvoidp{renderBufferDesc}, voidp{hWnd}, coopLevel,
        voidp{fullDuplex}, voidp{captureBuffer8}, voidp{renderBuffer8}, voidp{outer});
    return E_NOTIMPL;
}

} // extern "C"

namespace {

std::mutex gDeviceListMutex;
std::deque<GUID> gPlaybackDevices;
std::deque<GUID> gCaptureDevices;

template<typename T>
HRESULT enumerate_mmdev(const EDataFlow flow, std::deque<GUID> &devlist, T cb)
{
    static constexpr WCHAR primary_desc[] = L"Primary Sound Driver";

    ComWrapper com;

    ComPtr<IMMDeviceEnumerator> devenum;
    HRESULT hr{CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator, ds::out_ptr(devenum))};
    if(FAILED(hr))
    {
        ERR("enumerate_mmdev CoCreateInstance failed: %08lx\n", hr);
        return hr;
    }

    ComPtr<IMMDeviceCollection> coll;
    hr = devenum->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, ds::out_ptr(coll));
    if(FAILED(hr))
    {
        WARN("enumerate_mmdev IMMDeviceEnumerator::EnumAudioEndpoints failed: %08lx\n", hr);
        return DS_OK;
    }

    UINT count{};
    hr = coll->GetCount(&count);
    if(FAILED(hr))
    {
        WARN("enumerate_mmdev IMMDeviceCollection::GetCount failed: %08lx\n", hr);
        return DS_OK;
    }

    if(count == 0)
        return DS_OK;

    std::deque<GUID>{}.swap(devlist);

    TRACE("enumerate_mmdev Calling back with NULL (%ls)\n", primary_desc);
    bool keep_going{cb(nullptr, primary_desc, L"")};

    auto send_device = [&devlist,&cb,&keep_going](IMMDevice *device)
    {
        ComPtr<IPropertyStore> ps;
        HRESULT hr2{device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps))};
        if(FAILED(hr2))
        {
            WARN("send_device IMMDevice::OpenPropertyStore failed: %08lx\n", hr2);
            return;
        }

        PropVariant pv;
        hr2 = ps->GetValue(PKEY_AudioEndpoint_GUID, pv.get());
        if(FAILED(hr2) || pv->vt != VT_LPWSTR)
        {
            WARN("send_device IPropertyStore::GetValue(GUID) failed: %08lx\n", hr2);
            return;
        }

        GUID guid;
        CLSIDFromString(pv->pwszVal, &guid);
        if(!devlist.empty() && devlist[0] == guid)
            return;

        devlist.emplace_back(guid);
        if(!keep_going) return;

        pv.clear();
        hr2 = ps->GetValue(reinterpret_cast<const PROPERTYKEY&>(DEVPKEY_Device_FriendlyName),
            pv.get());
        if(FAILED(hr2))
        {
            WARN("send_device IPropertyStore::GetValue(FriendlyName) failed: %08lx\n", hr2);
            return;
        }

        TRACE("send_device Calling back with %s - %ls\n", GuidPrinter{devlist.back()}.c_str(),
            pv->pwszVal);
        keep_going = cb(&devlist.back(), pv->pwszVal, aldriver_name);
    };

    ComPtr<IMMDevice> device;
    hr = devenum->GetDefaultAudioEndpoint(flow, eMultimedia, ds::out_ptr(device));
    if(SUCCEEDED(hr))
        send_device(device.get());

    for(UINT i{0};i < count;++i)
    {
        hr = coll->Item(i, ds::out_ptr(device));
        if(FAILED(hr))
        {
            WARN("enumerate_mmdev IMMDeviceCollection::Item failed: %08lx\n", hr);
            continue;
        }

        send_device(device.get());
    }

    return keep_going ? S_OK : S_FALSE;
}

} // namespace

extern "C" {

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
    TRACE("DllGetClassObject (%s, %s, %p)\n", GuidPrinter{rclsid}.c_str(),
        GuidPrinter{riid}.c_str(), voidp{ppv});
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_GetDeviceID(const GUID *guidSrc, GUID *guidDst) noexcept
{
    TRACE("GetDeviceID (%s, %p)\n", GuidPrinter{guidSrc}.c_str(), voidp{guidDst});

    if(!guidSrc || !guidDst)
        return DSERR_INVALIDPARAM;

    return GetDeviceID(*guidSrc, *guidDst);
}

} // extern "C"
