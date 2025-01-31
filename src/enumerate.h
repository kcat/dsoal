#ifndef ENUMERATE_H
#define ENUMERATE_H

#include <bit>
#include <deque>
#include <mutex>

#include <dsound.h>
#include <devpkey.h>
#include <mmdeviceapi.h>

#include "comhelpers.h"
#include "comptr.h"
#include "dsoal.h"
#include "guidprinter.h"
#include "logging.h"


inline constexpr WCHAR aldriver_name[] = L"dsoal-aldrv.dll"; /* NOLINT(*-avoid-c-arrays) */
inline constexpr WCHAR primary_desc[] = L"Primary Sound Driver"; /* NOLINT(*-avoid-c-arrays) */

inline std::mutex gDeviceListMutex;
inline std::deque<GUID> gPlaybackDevices;
inline std::deque<GUID> gCaptureDevices;

ComPtr<IMMDevice> GetMMDevice(ComWrapper&, EDataFlow flow, const GUID &id);

#define PREFIX "enumerate_mmdev "
template<typename T>
HRESULT enumerate_mmdev(const EDataFlow flow, std::deque<GUID> &devlist, T&& cb)
{
    ComWrapper com;

    ComPtr<IMMDeviceEnumerator> devenum;
    HRESULT hr{CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator, ds::out_ptr(devenum))};
    if(FAILED(hr))
    {
        ERR(PREFIX "CoCreateInstance failed: {:08x}", as_unsigned(hr));
        return hr;
    }

    ComPtr<IMMDeviceCollection> coll;
    hr = devenum->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, ds::out_ptr(coll));
    if(FAILED(hr))
    {
        WARN(PREFIX "IMMDeviceEnumerator::EnumAudioEndpoints failed: {:08x}", as_unsigned(hr));
        return DS_OK;
    }

    UINT count{};
    hr = coll->GetCount(&count);
    if(FAILED(hr))
    {
        WARN(PREFIX "IMMDeviceCollection::GetCount failed: {:08x}", as_unsigned(hr));
        return DS_OK;
    }

    if(count == 0)
        return DS_OK;

    std::deque<GUID>{}.swap(devlist);

    TRACE(PREFIX "Calling back with NULL ({})", wstr_to_utf8(std::data(primary_desc)));
    auto keep_going = bool{cb(nullptr, primary_desc, L"")};

    auto send_device = [&devlist,&cb,&keep_going](IMMDevice *device)
    {
        ComPtr<IPropertyStore> ps;
        HRESULT hr2{device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps))};
        if(FAILED(hr2))
        {
            WARN(PREFIX "IMMDevice::OpenPropertyStore failed: {:08x}", as_unsigned(hr2));
            return;
        }

        PropVariant pv;
        hr2 = ps->GetValue(PKEY_AudioEndpoint_GUID, pv.get());
        if(FAILED(hr2) || pv.type() != VT_LPWSTR)
        {
            WARN(PREFIX "IPropertyStore::GetValue(GUID) failed: {:08x}", as_unsigned(hr2));
            return;
        }

        GUID guid{};
        CLSIDFromString(pv.value<const WCHAR*>(), &guid);
        pv.clear();

        if(!devlist.empty() && devlist[0] == guid)
            return;

        devlist.emplace_back(guid);
        if(!keep_going) return;

        hr2 = ps->GetValue(std::bit_cast<PROPERTYKEY>(DEVPKEY_Device_FriendlyName), pv.get());
        if(FAILED(hr2))
        {
            WARN(PREFIX "IPropertyStore::GetValue(FriendlyName) failed: {:08x}", as_unsigned(hr2));
            return;
        }

        TRACE(PREFIX "Calling back with {} - {}", GuidPrinter{devlist.back()}.c_str(),
            wstr_to_utf8(pv.value<const WCHAR*>()));
        keep_going = cb(&devlist.back(), pv.value<const WCHAR*>(), std::data(aldriver_name));
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
            WARN(PREFIX "IMMDeviceCollection::Item failed: {:08x}", as_unsigned(hr));
            continue;
        }

        send_device(device.get());
    }

    return keep_going ? S_OK : S_FALSE;
}
#undef PREFIX

#endif // ENUMERATE_H
