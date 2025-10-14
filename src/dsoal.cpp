#define INITGUID

#include "dsoal.h"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string_view>
#include <vector>

#include <dsound.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

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

#define PREFIX "load_function "
template<typename T>
bool load_function(T &func, const char *name)
{
    func = std::bit_cast<T>(GetProcAddress(gOpenalHandle, name));
    if(!func) [[unlikely]]
    {
        ERR("Couldn't find {} in {}", name, wstr_to_utf8(std::data(aldriver_name)));
        return false;
    }
    return true;
}
#undef PREFIX

template<typename T>
void load_alcfunction(T &func, const char *name)
{ func = std::bit_cast<T>(alcGetProcAddress(nullptr, name)); }

#define PREFIX "load_openal "
bool load_openal()
{
    gOpenalHandle = LoadLibraryW(std::data(aldriver_name));
    if(!gOpenalHandle)
    {
        ERR("Couldn't load {}: {}", wstr_to_utf8(std::data(aldriver_name)), GetLastError());
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
        WARN("Unloading {}", wstr_to_utf8(std::data(aldriver_name)));
        if(gOpenalHandle)
            FreeLibrary(gOpenalHandle);
        gOpenalHandle = nullptr;
        return false;
    }

    TRACE("load_openal Loaded {}", wstr_to_utf8(std::data(aldriver_name)));

    if(!alcIsExtensionPresent(nullptr, "ALC_EXT_direct_context"))
    {
        ERR("Required ALC_EXT_direct_context not supported in {}",
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
#undef PREFIX

} // namespace

auto wstr_to_utf8(std::wstring_view wstr) -> std::string
{
    auto ret = std::string{};

    const auto u16len = ds::saturate_cast<int>(wstr.size());
    /* NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) */
    const auto u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), u16len, nullptr, 0, nullptr,
        nullptr);
    if(u8len > 0)
    {
        ret.resize(static_cast<size_t>(u8len));
        /* NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) */
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), u16len, ret.data(), u8len, nullptr,
            nullptr);
    }

    return ret;
}

auto utf8_to_wstr(std::string_view str) -> std::wstring
{
    auto ret = std::wstring{};

    const auto u8len = ds::saturate_cast<int>(str.size());
    /* NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) */
    const auto u16len = MultiByteToWideChar(CP_UTF8, 0, str.data(), u8len, nullptr, 0);
    if(u16len > 0)
    {
        ret.resize(static_cast<size_t>(u16len));
        /* NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) */
        MultiByteToWideChar(CP_UTF8, 0, str.data(), u8len, ret.data(), u16len);
    }

    return ret;
}


#define PREFIX "GetMMDevice "
ComPtr<IMMDevice> GetMMDevice(ComWrapper&, EDataFlow flow, const GUID &id)
{
    ComPtr<IMMDeviceEnumerator> devenum;
    HRESULT hr{CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator, ds::out_ptr(devenum))};
    if(FAILED(hr))
    {
        ERR("CoCreateInstance failed: {:08x}", as_unsigned(hr));
        return {};
    }

    ComPtr<IMMDeviceCollection> coll;
    hr = devenum->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, ds::out_ptr(coll));
    if(FAILED(hr))
    {
        WARN("IMMDeviceEnumerator::EnumAudioEndpoints failed: {:08x}", as_unsigned(hr));
        return {};
    }

    UINT count{};
    hr = coll->GetCount(&count);
    if(FAILED(hr))
    {
        WARN("IMMDeviceCollection::GetCount failed: {:08x}", as_unsigned(hr));
        return {};
    }

    for(UINT i{0};i < count;++i)
    {
        ComPtr<IMMDevice> device;
        hr = coll->Item(i, ds::out_ptr(device));
        if(FAILED(hr))
        {
            WARN("IMMDeviceCollection::Item failed: {:08x}", as_unsigned(hr));
            continue;
        }

        ComPtr<IPropertyStore> ps;
        hr = device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps));
        if(FAILED(hr))
        {
            WARN("IMMDevice::OpenPropertyStore failed: {:08x}", as_unsigned(hr));
            continue;
        }

        PropVariant pv;
        hr = ps->GetValue(PKEY_AudioEndpoint_GUID, pv.get());
        if(FAILED(hr) || pv.type() != VT_LPWSTR)
        {
            WARN("IPropertyStore::GetValue(GUID) failed: {:08x}", as_unsigned(hr));
            continue;
        }

        GUID devid{};
        CLSIDFromString(pv.value<const WCHAR*>(), &devid);
        if(id == devid) return device;
    }

    return {};
}
#undef PREFIX

#define PREFIX "GetDeviceID "
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

    auto const com = ComWrapper{};
    ComPtr<IMMDeviceEnumerator> devenum;
    HRESULT hr{CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator, ds::out_ptr(devenum))};
    if(FAILED(hr))
    {
        ERR("CoCreateInstance failed: {:08x}", as_unsigned(hr));
        return hr;
    }

    ComPtr<IMMDevice> device;
    hr = devenum->GetDefaultAudioEndpoint(flow, role, ds::out_ptr(device));
    if(FAILED(hr))
    {
        WARN("IMMDeviceEnumerator::GetDefaultAudioEndpoint failed: {:08x}", as_unsigned(hr));
        return DSERR_NODRIVER;
    }

    ComPtr<IPropertyStore> ps;
    hr = device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps));
    if(FAILED(hr))
    {
        WARN("IMMDevice::OpenPropertyStore failed: {:08x}", as_unsigned(hr));
        return hr;
    }

    PropVariant pv;
    hr = ps->GetValue(PKEY_AudioEndpoint_GUID, pv.get());
    if(FAILED(hr) || pv.type() != VT_LPWSTR)
    {
        WARN("IPropertyStore::GetValue(GUID) failed: {:08x}", as_unsigned(hr));
        return hr;
    }

    CLSIDFromString(pv.value<const WCHAR*>(), &guidDst);

    return DS_OK;
}
#undef PREFIX

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

#define PREFIX "DllMain "
DSOAL_EXPORT BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD reason, void *reserved)
{
    DEBUG("({}, {}, {})", voidp{hInstDLL}, reason, reserved);

    switch(reason)
    {
    case DLL_PROCESS_ATTACH:
        if(const WCHAR *wstr{_wgetenv(L"DSOAL_LOGFILE")}; wstr && *wstr != 0)
        {
            gLogFile.open(wstr);
            if(!gLogFile.is_open())
                ERR("Failed to open log file {}", wstr_to_utf8(wstr));
        }

        if(const char *str{std::getenv("DSOAL_LOGLEVEL")}; str && *str != 0)
        {
            char *endptr{};
            const auto level = std::strtol(str, &endptr, 0) + 1;
            if(!endptr || *endptr != 0)
                ERR("Invalid log level specified: \"{}\"", str);
            else
            {
                if(level < ds::to_underlying(LogLevel::Disable))
                    gLogLevel = LogLevel::Disable;
                else if(level > ds::to_underlying(LogLevel::Debug))
                    gLogLevel = LogLevel::Debug;
                else
                    gLogLevel = static_cast<LogLevel>(level);
            }
        }

        TRACE("Initializing library v{}-{} {}", DSOAL_VERSION, DSOAL_GIT_COMMIT_HASH,
            DSOAL_GIT_BRANCH);
        if(!load_openal())
        {
            gLogFile.close();
            return FALSE;
        }

        /* Increase refcount on dsound by 1 */
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
            reinterpret_cast<LPCWSTR>(hInstDLL), &hInstDLL);
        break;

    case DLL_PROCESS_DETACH:
        break;
    }

    return TRUE;
}
#undef PREFIX

#define PREFIX "DirectSoundCreate "
HRESULT WINAPI DSOAL_DirectSoundCreate(const GUID *deviceId, IDirectSound **ds, IUnknown *outer) noexcept
{
    TRACE("({}, {}, {})", GuidPrinter{deviceId}.c_str(), voidp{ds}, voidp{outer});

    if(!ds)
    {
        WARN("invalid parameter: ppDS == NULL");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("invalid parameter: pUnkOuter != NULL");
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
        ERR("Caught exception: {}", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}
#undef PREFIX

#define PREFIX "DirectSoundCreate8 "
HRESULT WINAPI DSOAL_DirectSoundCreate8(const GUID *deviceId, IDirectSound8 **ds, IUnknown *outer) noexcept
{
    TRACE("({}, {}, {})", DevidPrinter{deviceId}.c_str(), voidp{ds}, voidp{outer});

    if(!ds)
    {
        WARN("invalid parameter: ppDS == NULL");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("invalid parameter: pUnkOuter != NULL");
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
        ERR("Caught exception: {}", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}
#undef PREFIX

#define PREFIX "DirectSoundCaptureCreate "
HRESULT WINAPI DSOAL_DirectSoundCaptureCreate(const GUID *deviceId, IDirectSoundCapture **ds,
    IUnknown *outer) noexcept
{
    TRACE("({}, {}, {})", DevidPrinter{deviceId}.c_str(), voidp{ds}, voidp{outer});

    if(!ds)
    {
        WARN("invalid parameter: ppDS == NULL");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("invalid parameter: pUnkOuter != NULL");
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
        ERR("Caught exception: {}", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}
#undef PREFIX

#define PREFIX "DirectSoundCaptureCreate8 "
HRESULT WINAPI DSOAL_DirectSoundCaptureCreate8(const GUID *deviceId, IDirectSoundCapture8 **ds,
    IUnknown *outer) noexcept
{
    TRACE("({}, {}, {})", DevidPrinter{deviceId}.c_str(), voidp{ds}, voidp{outer});

    if(!ds)
    {
        WARN("invalid parameter: ppDS == NULL");
        return DSERR_INVALIDPARAM;
    }
    *ds = nullptr;

    if(outer)
    {
        WARN("invalid parameter: pUnkOuter != NULL");
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
        ERR("Caught exception: {}", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}
#undef PREFIX

#define PREFIX "DirectSoundFullDuplexCreate "
HRESULT WINAPI DSOAL_DirectSoundFullDuplexCreate(const GUID *captureDevice,
    const GUID *renderDevice, const DSCBUFFERDESC *captureBufferDesc,
    const DSBUFFERDESC *renderBufferDesc, HWND hWnd, DWORD coopLevel,
    IDirectSoundFullDuplex **fullDuplex, IDirectSoundCaptureBuffer8 **captureBuffer8,
    IDirectSoundBuffer8 **renderBuffer8, IUnknown *outer) noexcept
{
    TRACE("({}, {}, {}, {}, {}, {}, {}, {}, {}, {})", DevidPrinter{captureDevice}.c_str(),
        DevidPrinter{renderDevice}.c_str(), cvoidp{captureBufferDesc}, cvoidp{renderBufferDesc},
        voidp{hWnd}, coopLevel, voidp{fullDuplex}, voidp{captureBuffer8}, voidp{renderBuffer8},
        voidp{outer});

    if(renderBuffer8) *renderBuffer8 = nullptr;
    if(captureBuffer8) *captureBuffer8 = nullptr;
    if(!fullDuplex)
    {
        WARN("invalid out parameter: {}", voidp{fullDuplex});
        return DSERR_INVALIDPARAM;
    }
    *fullDuplex = nullptr;

    if(outer)
    {
        WARN("invalid parameter: pUnkOuter != NULL");
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
        ERR("Caught exception: {}", e.what());
        hr = DSERR_OUTOFMEMORY;
    }

    return hr;
}
#undef PREFIX

#define PREFIX "DirectSoundEnumerateA "
HRESULT WINAPI DSOAL_DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, void *userPtr) noexcept
{
    TRACE("({}, {})", std::bit_cast<void*>(callback), userPtr);

    if(!callback)
    {
        WARN("invalid parameter: callback == NULL");
        return DSERR_INVALIDPARAM;
    }

    auto do_enum = [=](GUID *const guid, std::wstring_view const dname,
        std::wstring_view const mname) -> bool
    {
        /* NOLINTBEGIN(bugprone-suspicious-stringview-data-usage) */
        auto const dlen = dname.empty() ? 0 : WideCharToMultiByte(CP_ACP, 0, dname.data(),
            ds::saturate_cast<int>(dname.size()), nullptr, 0, nullptr, nullptr);
        auto const mlen = mname.empty() ? 0 : WideCharToMultiByte(CP_ACP, 0, mname.data(),
            ds::saturate_cast<int>(mname.size()), nullptr, 0, nullptr, nullptr);
        if(dlen < 0 || mlen < 0)
        {
            ERR("Failed to convert device strings");
            return true;
        }

        auto descA = std::vector<char>(static_cast<size_t>(dlen+mlen)+2, '\0');
        auto *const modA = std::to_address(descA.begin() + dlen+1);

        if(!dname.empty())
            WideCharToMultiByte(CP_ACP, 0, dname.data(), ds::saturate_cast<int>(dname.size()),
                descA.data(), dlen, nullptr, nullptr);
        if(!mname.empty())
            WideCharToMultiByte(CP_ACP, 0, mname.data(), ds::saturate_cast<int>(mname.size()),
                modA, mlen, nullptr, nullptr);
        /* NOLINTEND(bugprone-suspicious-stringview-data-usage) */

        return callback(guid, descA.data(), modA, userPtr) != FALSE;
    };

    auto const listlock = std::lock_guard{gDeviceListMutex};
    HRESULT hr{enumerate_mmdev(eRender, gPlaybackDevices, do_enum)};
    return SUCCEEDED(hr) ? DS_OK : hr;
}
#undef PREFIX

#define PREFIX "DirectSoundEnumerateW "
HRESULT WINAPI DSOAL_DirectSoundEnumerateW(LPDSENUMCALLBACKW callback, void *userPtr) noexcept
{
    TRACE("({}, {})", std::bit_cast<void*>(callback), userPtr);

    if(!callback)
    {
        WARN("invalid parameter: callback == NULL");
        return DSERR_INVALIDPARAM;
    }

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *dname, const WCHAR *mname)
    { return callback(guid, dname, mname, userPtr) != FALSE; };

    auto const listlock = std::lock_guard{gDeviceListMutex};
    HRESULT hr{enumerate_mmdev(eRender, gPlaybackDevices, do_enum)};
    return SUCCEEDED(hr) ? DS_OK : hr;
}
#undef PREFIX

#define PREFIX "DirectSoundCaptureenumerateA "
HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateA(LPDSENUMCALLBACKA callback, void *userPtr) noexcept
{
    TRACE("({}, {})", std::bit_cast<void*>(callback), userPtr);

    if(!callback)
    {
        WARN("invalid parameter: callback == NULL");
        return DSERR_INVALIDPARAM;
    }

    auto do_enum = [=](GUID *const guid, std::wstring_view const dname,
        std::wstring_view const mname) -> bool
    {
        /* NOLINTBEGIN(bugprone-suspicious-stringview-data-usage) */
        auto const dlen = dname.empty() ? 0 : WideCharToMultiByte(CP_ACP, 0, dname.data(),
            ds::saturate_cast<int>(dname.size()), nullptr, 0, nullptr, nullptr);
        auto const mlen = mname.empty() ? 0 : WideCharToMultiByte(CP_ACP, 0, mname.data(),
            ds::saturate_cast<int>(mname.size()), nullptr, 0, nullptr, nullptr);
        if(dlen < 0 || mlen < 0)
        {
            ERR("Failed to convert capture device strings");
            return true;
        }

        auto descA = std::vector<char>(static_cast<size_t>(dlen+mlen)+2, '\0');
        auto *const modA = std::to_address(descA.begin() + dlen+1);

        if(!dname.empty())
            WideCharToMultiByte(CP_ACP, 0, dname.data(), ds::saturate_cast<int>(dname.size()),
                descA.data(), dlen, nullptr, nullptr);
        if(!mname.empty())
            WideCharToMultiByte(CP_ACP, 0, mname.data(), ds::saturate_cast<int>(mname.size()),
                modA, mlen, nullptr, nullptr);
        /* NOLINTEND(bugprone-suspicious-stringview-data-usage) */

        return callback(guid, descA.data(), modA, userPtr) != FALSE;
    };

    auto const listlock = std::lock_guard{gDeviceListMutex};
    HRESULT hr{enumerate_mmdev(eCapture, gCaptureDevices, do_enum)};
    return SUCCEEDED(hr) ? DS_OK : hr;
}
#undef PREFIX

#define PREFIX "DirectSoundCaptureEnumerateW "
HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW callback, void *userPtr) noexcept
{
    TRACE("({}, {})", std::bit_cast<void*>(callback), userPtr);

    if(!callback)
    {
        WARN("invalid parameter: callback == NULL");
        return DSERR_INVALIDPARAM;
    }

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *dname, const WCHAR *mname)
    { return callback(guid, dname, mname, userPtr) != FALSE; };

    auto const listlock = std::lock_guard{gDeviceListMutex};
    HRESULT hr{enumerate_mmdev(eCapture, gCaptureDevices, do_enum)};
    return SUCCEEDED(hr) ? DS_OK : hr;
}
#undef PREFIX

#define PREFIX "DllCanUnloadNow "
HRESULT WINAPI DSOAL_DllCanUnloadNow() noexcept
{
    TRACE("(): stub");
    return S_FALSE;
}
#undef PREFIX

#define PREFIX "DllGetClassObject "
HRESULT WINAPI DSOAL_DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) noexcept
{
    TRACE("({}, {}, {})", ClsidPrinter{rclsid}.c_str(), IidPrinter{riid}.c_str(), voidp{ppv});

    if(!ppv)
    {
        WARN("NULL out pointer");
        return E_INVALIDARG;
    }
    *ppv = nullptr;

    return Factory::GetFactory(rclsid, riid, ppv);
}
#undef PREFIX

#define PREFIX "GetDeviceID "
HRESULT WINAPI DSOAL_GetDeviceID(const GUID *guidSrc, GUID *guidDst) noexcept
{
    TRACE("({}, {})", DevidPrinter{guidSrc}.c_str(), voidp{guidDst});

    if(!guidSrc || !guidDst)
        return DSERR_INVALIDPARAM;

    return GetDeviceID(*guidSrc, *guidDst);
}
#undef PREFIX

} // extern "C"
