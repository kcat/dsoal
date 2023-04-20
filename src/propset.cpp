#include "propset.h"

#include <vfwmsgs.h>

#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;

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

    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX
#undef CLASS_PREFIX
