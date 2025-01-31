#define INITGUID

#include "dsoal.h"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>

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
#include "version.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

HMODULE gOpenalHandle{};


template<typename T>
bool load_function(T &func, const char *name)
{
    func = std::bit_cast<T>(GetProcAddress(gOpenalHandle, name));
    if(!func) UNLIKELY
    {
        ERR("load_function Couldn't find {} in {}", name, wstr_to_utf8(std::data(aldriver_name)));
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
        ERR("load_openal Couldn't load {}: {}", wstr_to_utf8(std::data(aldriver_name)),
            GetLastError());
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
#undef LOAD_FUNCPTR

    if(!ok)
    {
        WARN("load_openal Unloading {}", wstr_to_utf8(std::data(aldriver_name)));
        if(gOpenalHandle)
            FreeLibrary(gOpenalHandle);
        gOpenalHandle = nullptr;
        return false;
    }

    TRACE("load_openal Loaded {}", wstr_to_utf8(std::data(aldriver_name)));

    if(!alcIsExtensionPresent(nullptr, "ALC_EXT_direct_context"))
    {
        ERR("load_openal Required ALC_EXT_direct_context not supported in {}",
            wstr_to_utf8(std::data(aldriver_name)));
        if(gOpenalHandle)
            FreeLibrary(gOpenalHandle);
        gOpenalHandle = nullptr;
        return false;
    }

#define LOAD_FUNCPTR(f) load_alcfunction(f, #f)
    LOAD_FUNCPTR(alEnableDirect);
    LOAD_FUNCPTR(alDisableDirect);
    LOAD_FUNCPTR(alIsEnabledDirect);
    LOAD_FUNCPTR(alGetStringDirect);
    LOAD_FUNCPTR(alGetBooleanvDirect);
    LOAD_FUNCPTR(alGetIntegervDirect);
    LOAD_FUNCPTR(alGetFloatvDirect);
    LOAD_FUNCPTR(alGetDoublevDirect);
    LOAD_FUNCPTR(alGetBooleanDirect);
    LOAD_FUNCPTR(alGetIntegerDirect);
    LOAD_FUNCPTR(alGetFloatDirect);
    LOAD_FUNCPTR(alGetDoubleDirect);
    LOAD_FUNCPTR(alGetErrorDirect);
    LOAD_FUNCPTR(alIsExtensionPresentDirect);
    LOAD_FUNCPTR(alGetProcAddressDirect);
    LOAD_FUNCPTR(alGetEnumValueDirect);
    LOAD_FUNCPTR(alListenerfDirect);
    LOAD_FUNCPTR(alListener3fDirect);
    LOAD_FUNCPTR(alListenerfvDirect);
    LOAD_FUNCPTR(alListeneriDirect);
    LOAD_FUNCPTR(alListener3iDirect);
    LOAD_FUNCPTR(alListenerivDirect);
    LOAD_FUNCPTR(alGetListenerfDirect);
    LOAD_FUNCPTR(alGetListener3fDirect);
    LOAD_FUNCPTR(alGetListenerfvDirect);
    LOAD_FUNCPTR(alGetListeneriDirect);
    LOAD_FUNCPTR(alGetListener3iDirect);
    LOAD_FUNCPTR(alGetListenerivDirect);
    LOAD_FUNCPTR(alGenSourcesDirect);
    LOAD_FUNCPTR(alDeleteSourcesDirect);
    LOAD_FUNCPTR(alIsSourceDirect);
    LOAD_FUNCPTR(alSourcefDirect);
    LOAD_FUNCPTR(alSource3fDirect);
    LOAD_FUNCPTR(alSourcefvDirect);
    LOAD_FUNCPTR(alSourceiDirect);
    LOAD_FUNCPTR(alSource3iDirect);
    LOAD_FUNCPTR(alSourceivDirect);
    LOAD_FUNCPTR(alGetSourcefDirect);
    LOAD_FUNCPTR(alGetSource3fDirect);
    LOAD_FUNCPTR(alGetSourcefvDirect);
    LOAD_FUNCPTR(alGetSourceiDirect);
    LOAD_FUNCPTR(alGetSource3iDirect);
    LOAD_FUNCPTR(alGetSourceivDirect);
    LOAD_FUNCPTR(alSourcePlayvDirect);
    LOAD_FUNCPTR(alSourceStopvDirect);
    LOAD_FUNCPTR(alSourceRewindvDirect);
    LOAD_FUNCPTR(alSourcePausevDirect);
    LOAD_FUNCPTR(alSourcePlayDirect);
    LOAD_FUNCPTR(alSourceStopDirect);
    LOAD_FUNCPTR(alSourceRewindDirect);
    LOAD_FUNCPTR(alSourcePauseDirect);
    LOAD_FUNCPTR(alSourceQueueBuffersDirect);
    LOAD_FUNCPTR(alSourceUnqueueBuffersDirect);
    LOAD_FUNCPTR(alGenBuffersDirect);
    LOAD_FUNCPTR(alDeleteBuffersDirect);
    LOAD_FUNCPTR(alIsBufferDirect);
    LOAD_FUNCPTR(alBufferfDirect);
    LOAD_FUNCPTR(alBuffer3fDirect);
    LOAD_FUNCPTR(alBufferfvDirect);
    LOAD_FUNCPTR(alBufferiDirect);
    LOAD_FUNCPTR(alBuffer3iDirect);
    LOAD_FUNCPTR(alBufferivDirect);
    LOAD_FUNCPTR(alGetBufferfDirect);
    LOAD_FUNCPTR(alGetBuffer3fDirect);
    LOAD_FUNCPTR(alGetBufferfvDirect);
    LOAD_FUNCPTR(alGetBufferiDirect);
    LOAD_FUNCPTR(alGetBuffer3iDirect);
    LOAD_FUNCPTR(alGetBufferivDirect);
    LOAD_FUNCPTR(alBufferDataDirect);
    LOAD_FUNCPTR(alDopplerFactorDirect);
    LOAD_FUNCPTR(alDistanceModelDirect);
    LOAD_FUNCPTR(alSpeedOfSoundDirect);
    LOAD_FUNCPTR(EAXSetDirect);
    LOAD_FUNCPTR(EAXGetDirect);
    LOAD_FUNCPTR(alBufferDataStaticDirect);
#undef LOAD_FUNCPTR

    return true;
}

} // namespace

auto wstr_to_utf8(std::wstring_view wstr) -> std::string
{
    auto ret = std::string{};

    /* NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) */
    const auto len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), ds::sizei(wstr), nullptr, 0,
        nullptr, nullptr);
    if(len > 0)
    {
        ret.resize(static_cast<size_t>(len));
        /* NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) */
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), ds::sizei(wstr), ret.data(), len, nullptr,
            nullptr);
    }

    return ret;
}

auto utf8_to_wstr(std::string_view str) -> std::wstring
{
    auto ret = std::wstring{};

    /* NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) */
    const auto len = MultiByteToWideChar(CP_UTF8, 0, str.data(), ds::sizei(str), nullptr, 0);
    if(len > 0)
    {
        ret.resize(static_cast<size_t>(len));
        /* NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) */
        MultiByteToWideChar(CP_UTF8, 0, str.data(), ds::sizei(str), ret.data(), len);
    }

    return ret;
}


ComPtr<IMMDevice> GetMMDevice(ComWrapper&, EDataFlow flow, const GUID &id)
{
    ComPtr<IMMDeviceEnumerator> devenum;
    HRESULT hr{CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator, ds::out_ptr(devenum))};
    if(FAILED(hr))
    {
        ERR("GetMMDevice CoCreateInstance failed: {:08x}", as_unsigned(hr));
        return {};
    }

    ComPtr<IMMDeviceCollection> coll;
    hr = devenum->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, ds::out_ptr(coll));
    if(FAILED(hr))
    {
        WARN("GetMMDevice IMMDeviceEnumerator::EnumAudioEndpoints failed: {:08x}", as_unsigned(hr));
        return {};
    }

    UINT count{};
    hr = coll->GetCount(&count);
    if(FAILED(hr))
    {
        WARN("GetMMDevice IMMDeviceCollection::GetCount failed: {:08x}", as_unsigned(hr));
        return {};
    }

    for(UINT i{0};i < count;++i)
    {
        ComPtr<IMMDevice> device;
        hr = coll->Item(i, ds::out_ptr(device));
        if(FAILED(hr))
        {
            WARN("GetMMDevice IMMDeviceCollection::Item failed: {:08x}", as_unsigned(hr));
            continue;
        }

        ComPtr<IPropertyStore> ps;
        hr = device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps));
        if(FAILED(hr))
        {
            WARN("GetMMDevice IMMDevice::OpenPropertyStore failed: {:08x}", as_unsigned(hr));
            continue;
        }

        PropVariant pv;
        hr = ps->GetValue(PKEY_AudioEndpoint_GUID, pv.get());
        if(FAILED(hr) || pv.type() != VT_LPWSTR)
        {
            WARN("GetMMDevice IPropertyStore::GetValue(GUID) failed: {:08x}", as_unsigned(hr));
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
        ERR("GetDeviceID CoCreateInstance failed: {:08x}", as_unsigned(hr));
        return hr;
    }

    ComPtr<IMMDevice> device;
    hr = devenum->GetDefaultAudioEndpoint(flow, role, ds::out_ptr(device));
    if(FAILED(hr))
    {
        WARN("GetDeviceID IMMDeviceEnumerator::GetDefaultAudioEndpoint failed: {:08x}",
            as_unsigned(hr));
        return DSERR_NODRIVER;
    }

    ComPtr<IPropertyStore> ps;
    hr = device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps));
    if(FAILED(hr))
    {
        WARN("GetDeviceID IMMDevice::OpenPropertyStore failed: {:08x}", as_unsigned(hr));
        return hr;
    }

    PropVariant pv;
    hr = ps->GetValue(PKEY_AudioEndpoint_GUID, pv.get());
    if(FAILED(hr) || pv.type() != VT_LPWSTR)
    {
        WARN("GetDeviceID IPropertyStore::GetValue(GUID) failed: {:08x}", as_unsigned(hr));
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
    DEBUG("DllMain ({}, {}, {})", voidp{hInstDLL}, reason, reserved);

    switch(reason)
    {
    case DLL_PROCESS_ATTACH:
        if(const WCHAR *wstr{_wgetenv(L"DSOAL_LOGFILE")}; wstr && *wstr != 0)
        {
            gsl::owner<FILE*> f{_wfopen(wstr, L"wt")};
            if(!f) ERR("Failed to open log file {}", wstr_to_utf8(wstr));
            else gLogFile = f;
        }

        if(const char *str{std::getenv("DSOAL_LOGLEVEL")}; str && *str != 0)
        {
            char *endptr{};
            const auto level = std::strtol(str, &endptr, 0) + 1;
            if(!endptr || *endptr != 0)
                ERR("Invalid log level specified: \"{}\"", str);
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

        TRACE("DllMain Initializing library v{}-{} {}", DSOAL_VERSION, DSOAL_GIT_COMMIT_HASH,
            DSOAL_GIT_BRANCH);
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
    TRACE("DirectSoundCreate ({}, {}, {})", GuidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});

    if(!ds)
    {
        WARN("DirectSoundCreate invalid parameter: ppDS == NULL");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("DirectSoundCreate invalid parameter: pUnkOuter != NULL");
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
        ERR("DirectSoundCreate Caught exception: {}", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}

HRESULT WINAPI DSOAL_DirectSoundCreate8(const GUID *deviceId, IDirectSound8 **ds, IUnknown *outer) noexcept
{
    TRACE("DirectSoundCreate8 ({}, {}, {})", DevidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});

    if(!ds)
    {
        WARN("DirectSoundCreate8 invalid parameter: ppDS == NULL");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("DirectSoundCreate8 invalid parameter: pUnkOuter != NULL");
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
        ERR("DirectSoundCreate8 Caught exception: {}", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureCreate(const GUID *deviceId, IDirectSoundCapture **ds,
    IUnknown *outer) noexcept
{
    TRACE("DirectSoundCaptureCreate ({}, {}, {})", DevidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});

    if(!ds)
    {
        WARN("DirectSoundCaptureCreate invalid parameter: ppDS == NULL");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("DirectSoundCaptureCreate invalid parameter: pUnkOuter != NULL");
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
        ERR("DirectSoundCaptureCreate Caught exception: {}", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureCreate8(const GUID *deviceId, IDirectSoundCapture8 **ds,
    IUnknown *outer) noexcept
{
    TRACE("DirectSoundCaptureCreate8 ({}, {}, {})", DevidPrinter{deviceId}.c_str(), voidp{ds},
        voidp{outer});

    if(!ds)
    {
        WARN("DirectSoundCaptureCreate8 invalid parameter: ppDS == NULL");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("DirectSoundCaptureCreate8 invalid parameter: pUnkOuter != NULL");
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
        ERR("DirectSoundCaptureCreate8 Caught exception: {}", e.what());
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
    TRACE("DirectSoundFullDuplexCreate ({}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
        DevidPrinter{captureDevice}.c_str(), DevidPrinter{renderDevice}.c_str(),
        cvoidp{captureBufferDesc}, cvoidp{renderBufferDesc}, voidp{hWnd}, coopLevel,
        voidp{fullDuplex}, voidp{captureBuffer8}, voidp{renderBuffer8}, voidp{outer});

    if(renderBuffer8) *renderBuffer8 = nullptr;
    if(captureBuffer8) *captureBuffer8 = nullptr;
    if(!fullDuplex)
    {
        WARN("DirectSoundFullDuplexCreate invalid out parameter: {}", voidp{fullDuplex});
        return DSERR_INVALIDPARAM;
    }
    *fullDuplex = nullptr;

    if(outer)
    {
        WARN("DirectSoundFullDuplexCreate invalid parameter: pUnkOuter != NULL");
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
        ERR("DirectSoundFullDuplexCreate Caught exception: {}", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}


HRESULT WINAPI DSOAL_DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, void *userPtr) noexcept
{
    TRACE("DirectSoundEnumerateA ({}, {})", reinterpret_cast<void*>(callback), userPtr);

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
    TRACE("DirectSoundEnumerateW ({}, {})", reinterpret_cast<void*>(callback), userPtr);

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *drvname, const WCHAR *devname)
    { return callback(guid, drvname, devname, userPtr) != FALSE; };

    std::lock_guard listlock{gDeviceListMutex};
    return enumerate_mmdev(eRender, gPlaybackDevices, do_enum);
}

HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateA(LPDSENUMCALLBACKA callback, void *userPtr) noexcept
{
    TRACE("DirectSoundCaptureEnumerateA ({}, {})", reinterpret_cast<void*>(callback), userPtr);

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
    TRACE("DirectSoundCaptureEnumerateW ({}, {})", reinterpret_cast<void*>(callback), userPtr);

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *drvname, const WCHAR *devname)
    { return callback(guid, drvname, devname, userPtr) != FALSE; };

    std::lock_guard listlock{gDeviceListMutex};
    return enumerate_mmdev(eCapture, gCaptureDevices, do_enum);
}


HRESULT WINAPI DSOAL_DllCanUnloadNow() noexcept
{
    TRACE("DllCanUnloadNow");
    return S_FALSE;
}

HRESULT WINAPI DSOAL_DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) noexcept
{
    TRACE("DllGetClassObject ({}, {}, {})", ClsidPrinter{rclsid}.c_str(), IidPrinter{riid}.c_str(),
        voidp{ppv});

    if(!ppv)
    {
        WARN("DllGetClassObject NULL out pointer");
        return E_INVALIDARG;
    }
    *ppv = nullptr;

    return Factory::GetFactory(rclsid, riid, ppv);
}

HRESULT WINAPI DSOAL_GetDeviceID(const GUID *guidSrc, GUID *guidDst) noexcept
{
    TRACE("GetDeviceID ({}, {})", DevidPrinter{guidSrc}.c_str(), voidp{guidDst});

    if(!guidSrc || !guidDst)
        return DSERR_INVALIDPARAM;

    return GetDeviceID(*guidSrc, *guidDst);
}

} // extern "C"
