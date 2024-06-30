#include "propset.h"

#include <cstring>
#include <deque>
#include <string_view>

#include <dsconf.h>
#include <mmdeviceapi.h>
#include <vfwmsgs.h>

#include "enumerate.h"
#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;


WCHAR *strdupW(const std::wstring_view str)
{
    void *ret;

    const size_t numchars{str.size()+1};
    ret = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, numchars*sizeof(WCHAR));
    if(!ret) return nullptr;

    std::memcpy(ret, str.data(), numchars*sizeof(WCHAR));
    return static_cast<WCHAR*>(ret);
}


#define PREFIX "DSPROPERTY_WaveDeviceMappingW "
HRESULT DSPROPERTY_WaveDeviceMappingW(void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    auto ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA*>(pPropData);

    if(!ppd || cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA))
    {
        WARN(PREFIX "Invalid ppd %p, %lu\n", voidp{ppd}, cbPropData);
        return DSERR_INVALIDPARAM;
    }

    auto find_target = [ppd](const GUID *guid, const WCHAR *devname, const WCHAR*) -> bool
    {
        if(lstrcmpW(devname, ppd->DeviceName) == 0)
        {
            ppd->DeviceId = *guid;
            return false;
        }
        return true;
    };

    std::deque<GUID> devlist;
    HRESULT hr{};
    if(ppd->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_RENDER)
        hr = enumerate_mmdev(eRender, devlist, find_target);
    else if(ppd->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE)
        hr = enumerate_mmdev(eCapture, devlist, find_target);
    else
        return DSERR_INVALIDPARAM;

    /* If the enumeration return value isn't S_FALSE, the device name wasn't
     * found.
     */
    if(hr != S_FALSE)
        return DSERR_INVALIDPARAM;

    *pcbReturned = sizeof(*ppd);
    return DS_OK;
}
#undef PREFIX

#define PREFIX "DSPROPERTY_DescriptionW "
HRESULT DSPROPERTY_DescriptionW(void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    auto ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA*>(pPropData);

    if(!ppd || cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA))
    {
        WARN(PREFIX "Invalid ppd %p, %lu\n", voidp{ppd}, cbPropData);
        return E_PROP_ID_UNSUPPORTED;
    }

    EDataFlow flow{};
    if(ppd->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_RENDER)
        flow = eRender;
    else if(ppd->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE)
        flow = eCapture;
    else
    {
        WARN(PREFIX "Unhandled data flow: %u\n", ppd->DataFlow);
        return E_PROP_ID_UNSUPPORTED;
    }

    if(ppd->DeviceId == GUID_NULL)
    {
        if(flow == eRender)
            ppd->DeviceId = DSDEVID_DefaultPlayback;
        else
            ppd->DeviceId = DSDEVID_DefaultCapture;
    }

    GUID devid{};
    GetDeviceID(ppd->DeviceId, devid);

    ComWrapper com;
    auto device = GetMMDevice(com, flow, devid);
    if(!device) return DSERR_INVALIDPARAM;

    ComPtr<IPropertyStore> ps;
    HRESULT hr{device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps))};
    if(FAILED(hr))
    {
        WARN(PREFIX "IMMDevice::OpenPropertyStore failed: %08lx\n", hr);
        return hr;
    }

    PropVariant pv;
    hr = ps->GetValue(ds::bit_cast<PROPERTYKEY>(DEVPKEY_Device_FriendlyName), pv.get());
    if(FAILED(hr) || pv.type() != VT_LPWSTR)
    {
        WARN(PREFIX "IPropertyStore::GetValue(FriendlyName) failed: %08lx\n", hr);
        return hr;
    }

    ppd->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
    ppd->Description = strdupW(pv.value<std::wstring_view>());
    ppd->Module = strdupW(aldriver_name);
    ppd->Interface = strdupW(L"Interface");

    *pcbReturned = sizeof(*ppd);

    return DS_OK;
}
#undef PREFIX

#define PREFIX "DSPROPERTY_EnumerateW "
HRESULT DSPROPERTY_EnumerateW(void *pPropData, ULONG cbPropData, ULONG*)
{
    auto ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA*>(pPropData);

    if(!ppd || cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA)
        || !ppd->Callback)
    {
        WARN(PREFIX "Invalid ppd %p, %lu\n", voidp{ppd}, cbPropData);
        return E_PROP_ID_UNSUPPORTED;
    }

    auto enum_render = [ppd](const GUID *guid, const WCHAR *devname, const WCHAR *drvname) -> bool
    {
        if(!guid)
            return true;

        WCHAR ifacename[] = L"Interface";
        std::wstring devnamedup{devname};
        std::wstring drvnamedup{drvname};

        DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA data{};
        data.Type = DIRECTSOUNDDEVICE_TYPE_WDM;
        data.DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
        data.Module = &drvnamedup[0];
        data.Description = &devnamedup[0];
        data.Interface = ifacename;

        return ppd->Callback(&data, ppd->Context) != FALSE;
    };

    std::deque<GUID> devlist;
    HRESULT hr{enumerate_mmdev(eRender, devlist, enum_render)};
    if(hr == S_OK)
    {
        devlist.clear();

        auto enum_capture = [ppd](const GUID *guid, const WCHAR *devname, const WCHAR *drvname)
            -> bool
        {
            if(!guid)
                return true;

            WCHAR ifacename[] = L"Interface";
            std::wstring devnamedup{devname};
            std::wstring drvnamedup{drvname};

            DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA data{};
            data.Type = DIRECTSOUNDDEVICE_TYPE_WDM;
            data.DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE;
            data.Module = &drvnamedup[0];
            data.Description = &devnamedup[0];
            data.Interface = ifacename;

            return ppd->Callback(&data, ppd->Context) != FALSE;
        };
        hr = enumerate_mmdev(eCapture, devlist, enum_capture);
    }

    return SUCCEEDED(hr) ? DS_OK : hr;

}
#undef PREFIX

} // namespace


#define CLASS_PREFIX "DSPrivatePropertySet::"
DSPrivatePropertySet::DSPrivatePropertySet() = default;
DSPrivatePropertySet::~DSPrivatePropertySet() = default;

ComPtr<DSPrivatePropertySet> DSPrivatePropertySet::Create()
{
    return ComPtr<DSPrivatePropertySet>{new DSPrivatePropertySet{}};
}


#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE DSPrivatePropertySet::QueryInterface(REFIID riid, void **ppvObject) noexcept
{
    DEBUG(PREFIX "(%p)->(%s, %p)\n", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    *ppvObject = nullptr;
    if(riid == IID_IUnknown)
    {
        AddRef();
        *ppvObject = as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IKsPropertySet)
    {
        AddRef();
        *ppvObject = as<IKsPropertySet*>();
        return S_OK;
    }

    FIXME(PREFIX "Unhandled GUID: %s\n", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

ULONG STDMETHODCALLTYPE DSPrivatePropertySet::AddRef() noexcept
{
    const auto ret = mRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(CLASS_PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSPrivatePropertySet::Release() noexcept
{
    const auto ret = mRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(CLASS_PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(ret == 0) UNLIKELY delete this;
    return ret;
}


#define PREFIX CLASS_PREFIX "Get "
HRESULT STDMETHODCALLTYPE DSPrivatePropertySet::Get(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData,
    ULONG *pcbReturned) noexcept
{
    DEBUG(PREFIX "(%p)->(%s, 0x%lx, %p, %lu, %p, %lu, %p)\n", voidp{this},
        PropidPrinter{guidPropSet}.c_str(), dwPropID, pInstanceData, cbInstanceData, pPropData,
        cbPropData, voidp{pcbReturned});

    if(!pcbReturned)
        return E_POINTER;
    *pcbReturned = 0;

    if(cbPropData > 0 && !pPropData)
    {
        WARN(PREFIX "pPropData is null with cbPropData > 0\n");
        return E_POINTER;
    }

    if(guidPropSet == DSPROPSETID_DirectSoundDevice)
    {
        switch(dwPropID)
        {
#if 0
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A:
            return DSPROPERTY_WaveDeviceMappingA(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1:
            return DSPROPERTY_Description1(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1:
            return DSPROPERTY_Enumerate1(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A:
            return DSPROPERTY_DescriptionA(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A:
            return DSPROPERTY_EnumerateA(pPropData, cbPropData, pcbReturned);
#endif
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W:
            return DSPROPERTY_WaveDeviceMappingW(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W:
            return DSPROPERTY_DescriptionW(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W:
            return DSPROPERTY_EnumerateW(pPropData, cbPropData, pcbReturned);
        default:
            FIXME(PREFIX "unsupported ID: %ld\n",dwPropID);
            return E_PROP_ID_UNSUPPORTED;
        }
    }

    FIXME(PREFIX "Unhandled propset: %s (propid: %lu)\n", PropidPrinter{guidPropSet}.c_str(),
        dwPropID);
    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Set "
HRESULT STDMETHODCALLTYPE DSPrivatePropertySet::Set(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData) noexcept
{
    DEBUG(PREFIX "(%p)->(%s, 0x%lx, %p, %lu, %p, %lu)\n", voidp{this},
        PropidPrinter{guidPropSet}.c_str(), dwPropID, pInstanceData, cbInstanceData, pPropData,
        cbPropData);
    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "QuerySupport "
HRESULT STDMETHODCALLTYPE DSPrivatePropertySet::QuerySupport(REFGUID guidPropSet, ULONG dwPropID,
    ULONG *pTypeSupport) noexcept
{
    FIXME(PREFIX "(%p)->(%s, 0x%lx, %p)\n", voidp{this}, PropidPrinter{guidPropSet}.c_str(),
        dwPropID, voidp{pTypeSupport});

    if(!pTypeSupport)
        return E_POINTER;
    *pTypeSupport = 0;

    if(guidPropSet == DSPROPSETID_DirectSoundDevice)
    {
        switch(dwPropID)
        {
#if 0
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A:
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1:
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1:
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A:
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A:
#endif
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W:
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W:
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W:
            *pTypeSupport = KSPROPERTY_SUPPORT_GET;
            return S_OK;
        default:
            FIXME(PREFIX "unsupported ID: %ld\n",dwPropID);
            return E_PROP_ID_UNSUPPORTED;
        }
    }

    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX
#undef CLASS_PREFIX
