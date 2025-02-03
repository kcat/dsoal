#include "propset.h"

#include <bit>
#include <cstring>
#include <deque>
#include <string_view>
#include <vector>

#include <dsconf.h>
#include <mmdeviceapi.h>
#include <vfwmsgs.h>

#include "enumerate.h"
#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;


auto DSPROPERTY_descWtoA(const DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA *dataW,
    DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA *dataA) -> bool
{
    static char Interface[] = "Interface"; /* NOLINT(*-avoid-c-arrays) */

    const auto modlen = WideCharToMultiByte(CP_ACP, 0, dataW->Module, -1, nullptr, 0, nullptr,
        nullptr);
    const auto desclen = WideCharToMultiByte(CP_ACP, 0, dataW->Description, -1, nullptr, 0,
        nullptr, nullptr);
    if(modlen < 0 || desclen < 0)
        return false;

    /* NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc) */
    dataA->Type = dataW->Type;
    dataA->DataFlow = dataW->DataFlow;
    dataA->DeviceId = dataW->DeviceId;
    dataA->WaveDeviceId = dataW->WaveDeviceId;
    dataA->Interface = std::data(Interface);
    dataA->Module = static_cast<LPSTR>(malloc(static_cast<size_t>(modlen)));
    dataA->Description = static_cast<LPSTR>(malloc(static_cast<size_t>(desclen)));
    if(!dataA->Module || !dataA->Description)
    {
        free(dataA->Module);
        dataA->Module = nullptr;
        free(dataA->Description);
        dataA->Description = nullptr;
        return false;
    }
    /* NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc) */

    WideCharToMultiByte(CP_ACP, 0, dataW->Module, -1, dataA->Module, modlen, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, dataW->Description, -1, dataA->Description, desclen, nullptr,
        nullptr);
    return true;
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
    TRACE("(pPropData={}, cbPropData={}, pcbReturned={})", pPropData, cbPropData,
        voidp{pcbReturned});

    if(!pPropData || cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA))
    {
        WARN("Invalid ppd {}, {}", pPropData, cbPropData);
        return DSERR_INVALIDPARAM;
    }

    auto ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA*>(pPropData);
    auto flow = eRender;
    auto *devlist = &gPlaybackDevices;
    if(ppd->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE)
    {
        flow = eCapture;
        devlist = &gCaptureDevices;
    }
    else if(ppd->DataFlow != DIRECTSOUNDDEVICE_DATAFLOW_RENDER)
        return DSERR_INVALIDPARAM;

    auto find_target = [ppd](const GUID *guid, const WCHAR *devname, const WCHAR*) -> bool
    {
        if(lstrcmpW(devname, ppd->DeviceName) == 0)
        {
            ppd->DeviceId = *guid;
            return false;
        }
        return true;
    };

    auto listlock = std::lock_guard{gDeviceListMutex};
    const auto hr = enumerate_mmdev(flow, *devlist, find_target);
    if(hr != S_FALSE)
    {
        /* If the enumeration return value isn't S_FALSE, the device name
         * wasn't found.
         */
        return DSERR_INVALIDPARAM;
    }

    if(pcbReturned)
        *pcbReturned = sizeof(*ppd);
    return DS_OK;
}
#undef PREFIX

#define PREFIX "DSPROPERTY_WaveDeviceMappingA "
auto DSPROPERTY_WaveDeviceMappingA(void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
    -> HRESULT
{
    TRACE("(pPropData={}, cbPropData={}, pcbReturned={})", pPropData, cbPropData,
        voidp{pcbReturned});

    if(!pPropData || cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA))
    {
        WARN("Invalid ppd {}, {}", pPropData, cbPropData);
        return DSERR_INVALIDPARAM;
    }

    auto *ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA*>(pPropData);
    if(!ppd->DeviceName)
    {
        WARN("Null DeviceName");
        return DSERR_INVALIDPARAM;
    }

    const auto slen = MultiByteToWideChar(CP_ACP, 0, ppd->DeviceName, -1, nullptr, 0);
    if(slen < 0)
    {
        WARN("Failed to convert device name");
        return DSERR_INVALIDPARAM;
    }

    auto namestore = std::vector<WCHAR>(static_cast<size_t>(slen));
    auto data = DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA{};
    data.DataFlow = ppd->DataFlow;
    data.DeviceName = namestore.data();
    MultiByteToWideChar(CP_ACP, 0, ppd->DeviceName, -1, data.DeviceName, slen);

    const auto hr = DSPROPERTY_WaveDeviceMappingW(&data, cbPropData, nullptr);
    if(SUCCEEDED(hr))
    {
        ppd->DeviceId = data.DeviceId;
        if(pcbReturned)
            *pcbReturned = sizeof(*ppd);
    }
    return hr;
}
#undef PREFIX

#define PREFIX "DSPROPERTY_DescriptionW "
HRESULT DSPROPERTY_DescriptionW(void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    TRACE("(pPropData={}, cbPropData={}, pcbReturned={})", pPropData, cbPropData,
        voidp{pcbReturned});

    if(!pPropData || cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA))
    {
        WARN("Invalid ppd {}, {}", pPropData, cbPropData);
        return DSERR_INVALIDPARAM;
    }

    auto ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA*>(pPropData);
    if(ppd->DeviceId == GUID_NULL)
    {
        if(ppd->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_RENDER)
            ppd->DeviceId = DSDEVID_DefaultPlayback;
        else if(ppd->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE)
            ppd->DeviceId = DSDEVID_DefaultCapture;
        else
        {
            WARN("Unhandled data flow: {}", ds::to_underlying(ppd->DataFlow));
            return E_PROP_ID_UNSUPPORTED;
        }
    }

    auto com = ComWrapper{};
    auto devid = GUID{};
    GetDeviceID(ppd->DeviceId, devid);

    auto device = GetMMDevice(com, eRender, devid);
    if(!device)
    {
        device = GetMMDevice(com, eCapture, devid);
        if(!device) return DSERR_INVALIDPARAM;
    }

    ComPtr<IPropertyStore> ps;
    HRESULT hr{device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps))};
    if(FAILED(hr))
    {
        WARN("IMMDevice::OpenPropertyStore failed: {:08x}", as_unsigned(hr));
        return hr;
    }

    PropVariant pv;
    hr = ps->GetValue(std::bit_cast<PROPERTYKEY>(DEVPKEY_Device_FriendlyName), pv.get());
    if(FAILED(hr) || pv.type() != VT_LPWSTR)
    {
        WARN("IPropertyStore::GetValue(FriendlyName) failed: {:08x}", as_unsigned(hr));
        return hr;
    }

    ppd->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
    ppd->Description = wcsdup(pv.value<const WCHAR*>());
    ppd->Module = wcsdup(std::data(aldriver_name));
    ppd->Interface = wcsdup(L"Interface");

    if(pcbReturned)
        *pcbReturned = sizeof(*ppd);
    return DS_OK;
}
#undef PREFIX

#define PREFIX "DSPROPERTY_DescriptionA "
auto DSPROPERTY_DescriptionA(void *pPropData, ULONG cbPropData, ULONG *pcbReturned) -> HRESULT
{
    TRACE("(pPropData={}, cbPropData={}, pcbReturned={})", pPropData, cbPropData,
        voidp{pcbReturned});

    if(pcbReturned)
        *pcbReturned = sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA);
    if(!pPropData)
        return S_OK;

    if(cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA))
        return E_INVALIDARG;
    auto ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA*>(pPropData);

    auto data = DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA{};
    data.DeviceId = ppd->DeviceId;
    data.DataFlow = ppd->DataFlow;
    auto hr = DSPROPERTY_DescriptionW(&data, sizeof(data), nullptr);
    if(FAILED(hr)) return hr;

    if(!DSPROPERTY_descWtoA(&data, ppd))
        hr = E_OUTOFMEMORY;
    /* NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc) */
    free(data.Description);
    free(data.Module);
    free(data.Interface);
    /* NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc) */

    return hr;
}
#undef PREFIX

#define PREFIX "DSPROPERTY_Description1 "
auto DSPROPERTY_Description1(void *pPropData, ULONG cbPropData, ULONG *pcbReturned) -> HRESULT
{
    TRACE("(pPropData={}, cbPropData={}, pcbReturned={})", pPropData, cbPropData,
        voidp{pcbReturned});

    if(pcbReturned)
        *pcbReturned = sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA);
    if(!pPropData)
        return S_OK;

    if(cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA))
        return DSERR_INVALIDPARAM;
    auto *ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA*>(pPropData);

    auto data = DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA{};
    data.DeviceId = ppd->DeviceId;
    data.DataFlow = ppd->DataFlow;
    const auto hr = DSPROPERTY_DescriptionW(&data, sizeof(data), nullptr);
    if(FAILED(hr)) return hr;

    DSPROPERTY_descWto1(&data, ppd);
    /* NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc) */
    free(data.Description);
    free(data.Module);
    free(data.Interface);
    /* NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc) */

    return hr;
}
#undef PREFIX

#define PREFIX "DSPROPERTY_EnumerateW "
auto DSPROPERTY_EnumerateW(void *pPropData, ULONG cbPropData, ULONG *pcbReturned) -> HRESULT
{
    TRACE("(pPropData={}, cbPropData={}, pcbReturned={})", pPropData, cbPropData,
        voidp{pcbReturned});

    if(!pPropData || cbPropData < sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA))
    {
        WARN("Invalid ppd {}, {}", pPropData, cbPropData);
        return E_PROP_ID_UNSUPPORTED;
    }

    auto ppd = static_cast<DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA*>(pPropData);
    if(!ppd->Callback)
    {
        WARN("Null callback");
        return E_PROP_ID_UNSUPPORTED;
    }

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

    auto listlock = std::lock_guard{gDeviceListMutex};
    auto hr = enumerate_mmdev(eRender, gPlaybackDevices, enum_render);
    if(hr == S_OK)
    {
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
        hr = enumerate_mmdev(eCapture, gCaptureDevices, enum_capture);
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
    DEBUG("({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    if(!ppvObject)
        return E_POINTER;
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

    FIXME("Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSPrivatePropertySet::AddRef() noexcept
{
    const auto ret = mRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSPrivatePropertySet::Release() noexcept
{
    const auto ret = mRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    if(ret == 0) [[unlikely]] delete this;
    return ret;
}
#undef PREFIX


#define PREFIX CLASS_PREFIX "Get "
HRESULT STDMETHODCALLTYPE DSPrivatePropertySet::Get(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData,
    ULONG *pcbReturned) noexcept
{
    TRACE("({})->({}, {:#x}, {}, {}, {}, {}, {})", voidp{this}, PropidPrinter{guidPropSet}.c_str(),
        dwPropID, pInstanceData, cbInstanceData, pPropData, cbPropData, voidp{pcbReturned});

    if(pcbReturned)
        *pcbReturned = 0;

    if(guidPropSet == DSPROPSETID_DirectSoundDevice)
    {
        switch(dwPropID)
        {
#if 0
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1:
            return DSPROPERTY_Enumerate1(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A:
            return DSPROPERTY_EnumerateA(pPropData, cbPropData, pcbReturned);
#endif
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A:
            return DSPROPERTY_WaveDeviceMappingA(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1:
            return DSPROPERTY_Description1(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W:
            return DSPROPERTY_WaveDeviceMappingW(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A:
            return DSPROPERTY_DescriptionA(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W:
            return DSPROPERTY_DescriptionW(pPropData, cbPropData, pcbReturned);
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W:
            return DSPROPERTY_EnumerateW(pPropData, cbPropData, pcbReturned);
        default:
            FIXME("unsupported ID: {}",dwPropID);
            return E_PROP_ID_UNSUPPORTED;
        }
    }

    FIXME("Unhandled propset: {} (propid: {})", PropidPrinter{guidPropSet}.c_str(), dwPropID);
    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Set "
HRESULT STDMETHODCALLTYPE DSPrivatePropertySet::Set(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData) noexcept
{
    TRACE("({})->({}, {:#x}, {}, {}, {}, {})", voidp{this}, PropidPrinter{guidPropSet}.c_str(),
        dwPropID, pInstanceData, cbInstanceData, pPropData, cbPropData);
    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "QuerySupport "
HRESULT STDMETHODCALLTYPE DSPrivatePropertySet::QuerySupport(REFGUID guidPropSet, ULONG dwPropID,
    ULONG *pTypeSupport) noexcept
{
    TRACE("({})->({}, {:#x}, {})", voidp{this}, PropidPrinter{guidPropSet}.c_str(), dwPropID,
        voidp{pTypeSupport});

    if(!pTypeSupport)
        return E_POINTER;
    *pTypeSupport = 0;

    if(guidPropSet == DSPROPSETID_DirectSoundDevice)
    {
        switch(dwPropID)
        {
#if 0
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1:
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A:
#endif
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A:
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1:
        case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W:
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A:
        case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W:
        case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W:
            *pTypeSupport = KSPROPERTY_SUPPORT_GET;
            return S_OK;
        default:
            FIXME("unsupported ID: {}",dwPropID);
            return E_PROP_ID_UNSUPPORTED;
        }
    }

    FIXME("Unhandled propset: {} (propid: {})", PropidPrinter{guidPropSet}.c_str(), dwPropID);
    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX
#undef CLASS_PREFIX
