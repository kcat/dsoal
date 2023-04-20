#ifndef PROPSET_H
#define PROPSET_H

#include <atomic>

#include <dsound.h>

#include "comptr.h"


inline constexpr GUID CLSID_DirectSoundPrivate{
    0x11ab3ec0,
    0x25ec,0x11d1,
    {0xa4,0xd8,0x00,0xc0,0x4f,0xc2,0x8a,0xca}
};

inline constexpr GUID DSPROPSETID_DirectSoundDevice{
    0x84624f82,
    0x25ec,0x11d1,
    {0xa4,0xd8,0x00,0xc0,0x4f,0xc2,0x8a,0xca}
};


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
