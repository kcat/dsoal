#ifndef CAPTURE_H
#define CAPTURE_H

#include <atomic>

#include <dsound.h>

#include "comptr.h"


class DSCapture final : IDirectSoundCapture {
    explicit DSCapture(bool is8);
    ~DSCapture();

    class Unknown final : IUnknown {
        DSCapture *impl_from_base() noexcept
        {
#ifdef __GNUC__
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wcast-align\"")
#endif
            return CONTAINING_RECORD(this, DSCapture, mUnknownIface);
#ifdef __GNUC__
    _Pragma("GCC diagnostic pop")
#endif
        }

    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
        ULONG STDMETHODCALLTYPE AddRef() noexcept override;
        ULONG STDMETHODCALLTYPE Release() noexcept override;

        template<typename T>
        T as() noexcept { return static_cast<T>(this); }
    };
    Unknown mUnknownIface;

    std::atomic<ULONG> mTotalRef{1u}, mDsRef{1u}, mUnkRef{0u};

    bool mIs8{};

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE CreateCaptureBuffer(const DSCBUFFERDESC *dscBufferDesc, IDirectSoundCaptureBuffer **dsCaptureBuffer, IUnknown *unk) noexcept override;
    HRESULT STDMETHODCALLTYPE GetCaps(DSCCAPS *dscCaps) noexcept override;
    HRESULT STDMETHODCALLTYPE Initialize(const GUID *guid) noexcept override;

    template<typename T> [[nodiscard]]
    T as() noexcept { return static_cast<T>(this); }

    static ComPtr<DSCapture> Create(bool is8);
};

#endif // CAPTURE_H
