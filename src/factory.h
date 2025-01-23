#ifndef FACTORY_H
#define FACTORY_H

#include <atomic>

#include <windows.h>


class Factory final : IClassFactory {
    using FunctionType = HRESULT(&)(REFIID riid, void **ppvObject);

    std::atomic<ULONG> mRef{0u};

    const GUID &mId;
    FunctionType mCreateInstance;

public:
    Factory(const GUID &id, FunctionType func) : mId{id}, mCreateInstance{func} { }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *unkOuter, REFIID riid, void **ppvObject) noexcept override;
    HRESULT STDMETHODCALLTYPE LockServer(BOOL dolock) noexcept override;

    [[nodiscard]]
    const GUID &getId() const noexcept { return mId; }

    template<typename U> [[nodiscard]]
    U as() noexcept { return static_cast<U>(this); }

    [[nodiscard]]
    static HRESULT GetFactory(const GUID &clsid, const GUID &iid, void **out);
};

#endif // FACTORY_H
