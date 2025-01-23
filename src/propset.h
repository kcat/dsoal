#ifndef PROPSET_H
#define PROPSET_H

#include <atomic>

#include <dsound.h>

#include "comptr.h"


class DSPrivatePropertySet final : IKsPropertySet {
    std::atomic<ULONG> mRef{1u};

public:
    DSPrivatePropertySet();
    ~DSPrivatePropertySet();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE Get(REFGUID guidPropSet, ULONG dwPropID, void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData, ULONG *pcbReturned) noexcept override;
    HRESULT STDMETHODCALLTYPE Set(REFGUID guidPropSet, ULONG dwPropID, void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData) noexcept override;
    HRESULT STDMETHODCALLTYPE QuerySupport(REFGUID guidPropSet, ULONG dwPropID, ULONG *pTypeSupport) noexcept override;

    template<typename T>
    T as() noexcept { return static_cast<T>(this); }

    static ComPtr<DSPrivatePropertySet> Create();
};

#endif // PROPSET_H
