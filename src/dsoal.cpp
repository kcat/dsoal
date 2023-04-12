#define INITGUID

#include "dsoal.h"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <devpkey.h>
#include <dsound.h>
#include <mmdeviceapi.h>
#include <mutex>

#include "comptr.h"
#include "logging.h"


extern "C" {

DSOAL_EXPORT BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD reason, void *reserved)
{
    DEBUG("DllMain (%p, %lu, %p)\n", hInstDLL, reason, reserved);

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
            const auto level = std::strtol(str, &endptr, 0);
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

        /* Increase refcount on dsound by 1 */
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)hInstDLL, &hInstDLL);
        break;

    case DLL_PROCESS_DETACH:
        if(gLogFile != stderr)
            fclose(gLogFile);
        gLogFile = stderr;
    }

    return TRUE;
}

HRESULT WINAPI DSOAL_DirectSoundCreate(const GUID *deviceId, IDirectSound **ds, IUnknown *outer)
{
    TRACE("DirectSoundCreate (%p, %p, %p)\n", deviceId, ds, outer);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundCreate8(const GUID *deviceId, IDirectSound8 **ds, IUnknown *outer)
{
    TRACE("DirectSoundCreate8 (%p, %p, %p)\n", deviceId, ds, outer);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureCreate(const GUID *deviceId, IDirectSoundCapture **ds,
    IUnknown *outer)
{
    TRACE("DirectSoundCaptureCreate (%p, %p, %p)\n", deviceId, ds, outer);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureCreate8(const GUID *deviceId, IDirectSoundCapture8 **ds,
    IUnknown *outer)
{
    TRACE("DirectSoundCaptureCreate8 (%p, %p, %p)\n", deviceId, ds, outer);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundFullDuplexCreate(const GUID *captureDevice,
    const GUID *renderDevice, const DSCBUFFERDESC *captureBufferDesc,
    const DSBUFFERDESC *renderBufferDesc, HWND hWnd, DWORD coopLevel,
    IDirectSoundFullDuplex **fullDuplex, IDirectSoundCaptureBuffer8 **captureBuffer8,
    IDirectSoundBuffer8 **renderBuffer8, IUnknown *outer)
{
    TRACE("DirectSoundFullDuplexCreate\n");
    return E_NOTIMPL;
}

} // extern "C"

namespace {

class GuidPrinter {
    char mMsg[64];

public:
    GuidPrinter(const GUID &guid)
    {
        std::snprintf(mMsg, std::size(mMsg), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            DWORD{guid.Data1}, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }
    const char *c_str() const { return mMsg; }
};

class PropVariant {
    PROPVARIANT mProp;

public:
    PropVariant() { PropVariantInit(&mProp); }
    ~PropVariant() { clear(); }

    void clear() { PropVariantClear(&mProp); }

    PROPVARIANT* get() noexcept { return &mProp; }

    PROPVARIANT& operator*() noexcept { return mProp; }
    const PROPVARIANT& operator*() const noexcept { return mProp; }

    PROPVARIANT* operator->() noexcept { return &mProp; }
    const PROPVARIANT* operator->() const noexcept { return &mProp; }
};

struct ComWrapper {
    HRESULT mStatus{};

    ComWrapper() { mStatus = CoInitialize(nullptr); }
    ComWrapper(ComWrapper&& rhs) { mStatus = std::exchange(rhs.mStatus, E_FAIL); }
    ComWrapper(const ComWrapper&) = delete;
    ~ComWrapper() { if(SUCCEEDED(mStatus)) CoUninitialize(); }

    ComWrapper& operator=(ComWrapper&& rhs)
    {
        if(SUCCEEDED(mStatus))
            CoUninitialize();
        mStatus = std::exchange(rhs.mStatus, E_FAIL);
        return *this;
    }
    ComWrapper& operator=(const ComWrapper&) = delete;
};

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

    auto send_device = [flow,&devlist,&cb,&keep_going](IMMDevice *device)
    {
        ComPtr<IPropertyStore> ps;
        HRESULT hr{device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps))};
        if(FAILED(hr))
        {
            WARN("send_device IMMDevice::OpenPropertyStore failed: %08lx\n", hr);
            return;
        }

        PropVariant pv;
        hr = ps->GetValue(PKEY_AudioEndpoint_GUID, pv.get());
        if(FAILED(hr) || pv->vt != VT_LPWSTR)
        {
            WARN("send_device IPropertyStore::GetValue(GUID) failed: %08lx\n", hr);
            return;
        }

        GUID guid;
        CLSIDFromString(pv->pwszVal, &guid);
        if(!devlist.empty() && devlist[0] == guid)
            return;

        devlist.emplace_back(guid);
        if(!keep_going) return;

        pv.clear();
        hr = ps->GetValue(reinterpret_cast<const PROPERTYKEY&>(DEVPKEY_Device_FriendlyName),
            pv.get());
        if(FAILED(hr))
        {
            WARN("send_device IPropertyStore::GetValue(FriendlyName) failed: %08lx\n", hr);
            return;
        }

        TRACE("send_device Calling back with %s - %ls\n", GuidPrinter{devlist.back()}.c_str(),
            pv->pwszVal);
        keep_going = cb(&devlist.back(), pv->pwszVal, L"dsoal-aldrv.dll");
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

HRESULT WINAPI DSOAL_DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, void *userPtr)
{
    TRACE("DirectSoundEnumerateA (%p, %p)\n", callback, userPtr);

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *drvname, const WCHAR *devname)
    {
        const auto dlen = WideCharToMultiByte(CP_ACP, 0, drvname, -1, nullptr, 0, nullptr, nullptr);
        const auto mlen = WideCharToMultiByte(CP_ACP, 0, devname, -1, nullptr, 0, nullptr, nullptr);
        if(dlen < 0 || mlen < 0) return false;

        auto descA = std::make_unique<char[]>(dlen+mlen+2);
        if(!descA) return false;
        char *modA = descA.get() + dlen+1;

        WideCharToMultiByte(CP_ACP, 0, drvname, -1, descA.get(), dlen, nullptr, nullptr);
        WideCharToMultiByte(CP_ACP, 0, devname, -1, modA, mlen, nullptr, nullptr);

        return callback(guid, descA.get(), modA, userPtr) != FALSE;
    };

    std::lock_guard listlock{gDeviceListMutex};
    return enumerate_mmdev(eRender, gPlaybackDevices, do_enum);
}
HRESULT WINAPI DSOAL_DirectSoundEnumerateW(LPDSENUMCALLBACKW callback, void *userPtr)
{
    TRACE("DirectSoundEnumerateW (%p, %p)\n", callback, userPtr);

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *drvname, const WCHAR *devname)
    { return callback(guid, drvname, devname, userPtr) != FALSE; };

    std::lock_guard listlock{gDeviceListMutex};
    return enumerate_mmdev(eRender, gPlaybackDevices, do_enum);
}

HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateA(LPDSENUMCALLBACKA callback, void *userPtr)
{
    TRACE("DirectSoundCaptureEnumerateA (%p, %p)\n", callback, userPtr);

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *drvname, const WCHAR *devname)
    {
        const auto dlen = WideCharToMultiByte(CP_ACP, 0, drvname, -1, nullptr, 0, nullptr, nullptr);
        const auto mlen = WideCharToMultiByte(CP_ACP, 0, devname, -1, nullptr, 0, nullptr, nullptr);
        if(dlen < 0 || mlen < 0) return false;

        auto descA = std::make_unique<char[]>(dlen+mlen+2);
        if(!descA) return false;
        char *modA = descA.get() + dlen+1;

        WideCharToMultiByte(CP_ACP, 0, drvname, -1, descA.get(), dlen, nullptr, nullptr);
        WideCharToMultiByte(CP_ACP, 0, devname, -1, modA, mlen, nullptr, nullptr);

        return callback(guid, descA.get(), modA, userPtr) != FALSE;
    };

    std::lock_guard listlock{gDeviceListMutex};
    return enumerate_mmdev(eCapture, gCaptureDevices, do_enum);
}
HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW callback, void *userPtr)
{
    TRACE("DirectSoundCaptureEnumerateW (%p, %p)\n", callback, userPtr);

    auto do_enum = [callback,userPtr](GUID *guid, const WCHAR *drvname, const WCHAR *devname)
    { return callback(guid, drvname, devname, userPtr) != FALSE; };

    std::lock_guard listlock{gDeviceListMutex};
    return enumerate_mmdev(eCapture, gCaptureDevices, do_enum);
}


HRESULT WINAPI DSOAL_DllCanUnloadNow()
{
    TRACE("DllCanUnloadNow\n");
    return S_OK;
}

HRESULT WINAPI DSOAL_DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    TRACE("DllGetClassObject (%p, %p, %p)\n", &rclsid, &riid, ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_GetDeviceID(const GUID *guidSrc, GUID *guidDst)
{
    TRACE("GetDeviceID (%p, %p)\n", guidSrc, guidDst);
    return E_NOTIMPL;
}

} // extern "C"
