#ifndef ENUMERATE_H
#define ENUMERATE_H

#include <deque>

#include <dsound.h>
#include <devpkey.h>
#include <mmdeviceapi.h>

#include "comhelpers.h"
#include "comptr.h"
#include "dsoal.h"
#include "guidprinter.h"
#include "logging.h"


inline constexpr WCHAR aldriver_name[] = L"dsoal-aldrv.dll";

ComPtr<IMMDevice> GetMMDevice(ComWrapper&, EDataFlow flow, const GUID &id);

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

        GUID guid{};
        CLSIDFromString(pv->pwszVal, &guid);
        pv.clear();

        if(!devlist.empty() && devlist[0] == guid)
            return;

        devlist.emplace_back(guid);
        if(!keep_going) return;

        hr2 = ps->GetValue(ds::bit_cast<PROPERTYKEY>(DEVPKEY_Device_FriendlyName), pv.get());
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

#endif // ENUMERATE_H
