#include "propset.h"

#include <bit>
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

void DSPROPERTY_descWto1(const DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA *dataW,
    DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA *data1)
{
    data1->DeviceId = dataW->DeviceId;
    lstrcpynW(std::data(data1->ModuleW), dataW->Module, std::size(data1->ModuleW));
    lstrcpynW(std::data(data1->DescriptionW), dataW->Description, std::size(data1->DescriptionW));
    WideCharToMultiByte(CP_ACP, 0, std::data(data1->DescriptionW), -1,
        std::data(data1->DescriptionA), sizeof(data1->DescriptionA)-1, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, std::data(data1->ModuleW), -1, std::data(data1->ModuleA),
        sizeof(data1->ModuleA)-1, nullptr, nullptr);
    data1->DescriptionA[sizeof(data1->DescriptionA)-1] = 0;
    data1->ModuleA[sizeof(data1->ModuleA)-1] = 0;
    data1->Type = dataW->Type;
    data1->DataFlow = dataW->DataFlow;
    data1->WaveDeviceId = dataW->WaveDeviceId;
    data1->Devnode = dataW->WaveDeviceId;
}


#define PREFIX "DSPROPERTY_WaveDeviceMappingW "
HRESULT DSPROPERTY_WaveDeviceMappingW(void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    auto ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA*>(pPropData);

    if(!ppd || cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA))
    {
        WARN(PREFIX "Invalid ppd {}, {}", voidp{ppd}, cbPropData);
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
        WARN(PREFIX "Invalid ppd {}, {}", voidp{ppd}, cbPropData);
        return E_PROP_ID_UNSUPPORTED;
    }

    EDataFlow flow{};
    if(ppd->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_RENDER)
        flow = eRender;
    else if(ppd->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE)
        flow = eCapture;
    else
    {
        WARN(PREFIX "Unhandled data flow: {}", ds::to_underlying(ppd->DataFlow));
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
        WARN(PREFIX "IMMDevice::OpenPropertyStore failed: {:08x}", as_unsigned(hr));
        return hr;
    }

    PropVariant pv;
    hr = ps->GetValue(std::bit_cast<PROPERTYKEY>(DEVPKEY_Device_FriendlyName), pv.get());
    if(FAILED(hr) || pv.type() != VT_LPWSTR)
    {
        WARN(PREFIX "IPropertyStore::GetValue(FriendlyName) failed: {:08x}", as_unsigned(hr));
        return hr;
    }

    ppd->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
    ppd->Description = strdupW(pv.value<std::wstring_view>());
    ppd->Module = strdupW(std::data(aldriver_name));
    ppd->Interface = strdupW(L"Interface");

    *pcbReturned = sizeof(*ppd);

    return DS_OK;
}
#undef PREFIX

auto DSPROPERTY_Description1(void *pPropData, ULONG cbPropData, ULONG *pcbReturned) -> HRESULT
{
    if(cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA))
        return E_INVALIDARG;

    if(!pPropData)
    {
        *pcbReturned = 0;
        return S_OK;
    }

    auto *ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA*>(pPropData);
    auto data = DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA{};
    auto retsize = ULONG{};

    data.DeviceId = ppd->DeviceId;
    data.DataFlow = ppd->DataFlow;
    const auto hr = DSPROPERTY_DescriptionW(&data, sizeof(data), &retsize);
    if(FAILED(hr)) return hr;

    DSPROPERTY_descWto1(&data, ppd);
    HeapFree(GetProcessHeap(), 0, data.Description);
    HeapFree(GetProcessHeap(), 0, data.Module);
    HeapFree(GetProcessHeap(), 0, data.Interface);

    *pcbReturned = sizeof(*ppd);
    return hr;
}


#define PREFIX "DSPROPERTY_EnumerateW "
HRESULT DSPROPERTY_EnumerateW(void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    auto ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA*>(pPropData);

    if(!ppd || cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA)
        || !ppd->Callback)
    {
        WARN(PREFIX "Invalid ppd {}, {}", voidp{ppd}, cbPropData);
        return E_PROP_ID_UNSUPPORTED;
    }

    *pcbReturned = 0;

    auto enum_render = [ppd](const GUID *guid, const WCHAR *devname, const WCHAR *drvname) -> bool
    {
        if(!guid)
            return true;

        std::wstring ifacename{L"Interface"};
        std::wstring devnamedup{devname};
        std::wstring drvnamedup{drvname};

        DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA data{};
        data.Type = DIRECTSOUNDDEVICE_TYPE_WDM;
        data.DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
        data.Module = drvnamedup.data();
        data.Description = devnamedup.data();
        data.Interface = ifacename.data();

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

            std::wstring ifacename{L"Interface"};
            std::wstring devnamedup{devname};
            std::wstring drvnamedup{drvname};

            DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA data{};
            data.Type = DIRECTSOUNDDEVICE_TYPE_WDM;
            data.DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE;
            data.Module = drvnamedup.data();
            data.Description = devnamedup.data();
            data.Interface = ifacename.data();

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
    DEBUG(PREFIX "({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

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

    FIXME(PREFIX "Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

ULONG STDMETHODCALLTYPE DSPrivatePropertySet::AddRef() noexcept
{
    const auto ret = mRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(CLASS_PREFIX "AddRef ({}) ref {}", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSPrivatePropertySet::Release() noexcept
{
    const auto ret = mRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(CLASS_PREFIX "Release ({}) ref {}", voidp{this}, ret);
    if(ret == 0) UNLIKELY delete this;
    return ret;
}


#define PREFIX CLASS_PREFIX "Get "
HRESULT STDMETHODCALLTYPE DSPrivatePropertySet::Get(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData,
    ULONG *pcbReturned) noexcept
{
    DEBUG(PREFIX "({})->({}, {:#x}, {}, {}, {}, {}, {})", voidp{this},
        PropidPrinter{guidPropSet}.c_str(), dwPropID, pInstanceData, cbInstanceData, pPropData,
        cbPropData, voidp{pcbReturned});

    if(!pcbReturned)
        return E_POINTER;
    *pcbReturned = 0;

    if(cbPropData > 0 && !pPropData)
    {
        WARN(PREFIX "pPropData is null with cbPropData > 0");
        return E_POINTER;
    }

    if(guidPropSet == DSPROPSETID_DirectSoundDevice)
    {
        switch(dwPropID)
        {
#if 0
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A:
            return DSPROPERTY_WaveDeviceMappingA(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1:
            return DSPROPERTY_Enumerate1(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A:
            return DSPROPERTY_DescriptionA(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A:
            return DSPROPERTY_EnumerateA(pPropData, cbPropData, pcbReturned);
#endif
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1:
            return DSPROPERTY_Description1(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W:
            return DSPROPERTY_WaveDeviceMappingW(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W:
            return DSPROPERTY_DescriptionW(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W:
            return DSPROPERTY_EnumerateW(pPropData, cbPropData, pcbReturned);
        default:
            FIXME(PREFIX "unsupported ID: {}",dwPropID);
            return E_PROP_ID_UNSUPPORTED;
        }
    }

    FIXME(PREFIX "Unhandled propset: {} (propid: {})", PropidPrinter{guidPropSet}.c_str(),
        dwPropID);
    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Set "
HRESULT STDMETHODCALLTYPE DSPrivatePropertySet::Set(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData) noexcept
{
    DEBUG(PREFIX "({})->({}, {:#x}, {}, {}, {}, {})", voidp{this},
        PropidPrinter{guidPropSet}.c_str(), dwPropID, pInstanceData, cbInstanceData, pPropData,
        cbPropData);
    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "QuerySupport "
HRESULT STDMETHODCALLTYPE DSPrivatePropertySet::QuerySupport(REFGUID guidPropSet, ULONG dwPropID,
    ULONG *pTypeSupport) noexcept
{
    FIXME(PREFIX "({})->({}, {:#x}, {})", voidp{this}, PropidPrinter{guidPropSet}.c_str(),
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
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1:
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A:
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A:
#endif
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1:
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W:
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W:
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W:
            *pTypeSupport = KSPROPERTY_SUPPORT_GET;
            return S_OK;
        default:
            FIXME(PREFIX "unsupported ID: {}",dwPropID);
            return E_PROP_ID_UNSUPPORTED;
        }
    }

    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX
#undef CLASS_PREFIX
