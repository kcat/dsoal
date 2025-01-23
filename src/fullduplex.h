#ifndef DSFULLDUPLEX_H
#define DSFULLDUPLEX_H

#include <atomic>
#include <bit>

#include <dsound.h>

#include "comptr.h"

class DSound8OAL;
class DSCapture;


class DSFullDuplex final : IDirectSoundFullDuplex {
    DSFullDuplex();

    class Unknown final : IUnknown {
        DSFullDuplex *impl_from_base() noexcept
        {
#ifdef __GNUC__
            _Pragma("GCC diagnostic push")
            _Pragma("GCC diagnostic ignored \"-Wcast-align\"")
#endif
            return CONTAINING_RECORD(this, DSFullDuplex, mUnknownIface);
#ifdef __GNUC__
            _Pragma("GCC diagnostic pop")
#endif
        }

    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
        ULONG STDMETHODCALLTYPE AddRef() noexcept override;
        ULONG STDMETHODCALLTYPE Release() noexcept override;

        Unknown() = default;
        Unknown(const Unknown&) = delete;
        Unknown& operator=(const Unknown&) = delete;

        template<typename T>
        T as() noexcept { return static_cast<T>(this); }
    };
    Unknown mUnknownIface;

    class DS8 final : IDirectSound8 {
        DSFullDuplex *impl_from_base() noexcept
        {
#ifdef __GNUC__
            _Pragma("GCC diagnostic push")
            _Pragma("GCC diagnostic ignored \"-Wcast-align\"")
#endif
            return CONTAINING_RECORD(this, DSFullDuplex, mDS8Iface);
#ifdef __GNUC__
            _Pragma("GCC diagnostic pop")
#endif
        }

    public:
        /*** IUnknown methods ***/
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
        ULONG STDMETHODCALLTYPE AddRef() noexcept override;
        ULONG STDMETHODCALLTYPE Release() noexcept override;
        /*** IDirectSound8 methods ***/
        HRESULT STDMETHODCALLTYPE CreateSoundBuffer(const DSBUFFERDESC *bufferDesc, IDirectSoundBuffer **dsBuffer, IUnknown *outer) noexcept override;
        HRESULT STDMETHODCALLTYPE GetCaps(DSCAPS *caps) noexcept override;
        HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(IDirectSoundBuffer *original, IDirectSoundBuffer **duplicate) noexcept override;
        HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hwnd, DWORD level) noexcept override;
        HRESULT STDMETHODCALLTYPE Compact() noexcept override;
        HRESULT STDMETHODCALLTYPE GetSpeakerConfig(DWORD *speakerConfig) noexcept override;
        HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD speakerConfig) noexcept override;
        HRESULT STDMETHODCALLTYPE Initialize(const GUID *deviceId) noexcept override;
        HRESULT STDMETHODCALLTYPE VerifyCertification(DWORD *certified) noexcept override;

        DS8() = default;
        DS8(const DS8&) = delete;
        DS8& operator=(const DS8&) = delete;

        template<typename T>
        T as() noexcept
        {
            /* MinGW headers do not have IDirectSound8 inherit from
             * IDirectSound, which MSVC apparently does. IDirectSound is a
             * strict subset of IDirectSound8, so the interface is ABI
             * compatible.
             */
            if constexpr(std::is_same_v<T,IDirectSound*> && !std::is_base_of_v<IDirectSound,DS8>)
                return std::bit_cast<T>(static_cast<IDirectSound8*>(this));
            else
                return static_cast<T>(this);
        }
    };
    DS8 mDS8Iface;

    class DSC final : IDirectSoundCapture {
        DSFullDuplex *impl_from_base() noexcept
        {
#ifdef __GNUC__
            _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wcast-align\"")
#endif
            return CONTAINING_RECORD(this, DSFullDuplex, mDSCIface);
#ifdef __GNUC__
            _Pragma("GCC diagnostic pop")
#endif
        }

    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
        ULONG STDMETHODCALLTYPE AddRef() noexcept override;
        ULONG STDMETHODCALLTYPE Release() noexcept override;
        HRESULT STDMETHODCALLTYPE CreateCaptureBuffer(const DSCBUFFERDESC *dscBufferDesc, IDirectSoundCaptureBuffer **dsCaptureBuffer, IUnknown *unk) noexcept override;
        HRESULT STDMETHODCALLTYPE GetCaps(DSCCAPS *dscCaps) noexcept override;
        HRESULT STDMETHODCALLTYPE Initialize(const GUID *guid) noexcept override;

        DSC() = default;
        DSC(const DSC&) = delete;
        DSC& operator=(const DSC&) = delete;

        template<typename T>
        T as() noexcept { return static_cast<T>(this); }
    };
    DSC mDSCIface;

    std::atomic<ULONG> mTotalRef{1u}, mFdRef{1u}, mDS8Ref{0u}, mDSCRef{0u}, mUnkRef{0u};

    ComPtr<DSound8OAL> mDS8Handle;
    ComPtr<DSCapture> mDSCHandle;

public:
    ~DSFullDuplex();

    DSFullDuplex(const DSFullDuplex&) = delete;
    DSFullDuplex& operator=(const DSFullDuplex&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE Initialize(const GUID *captureGuid, const GUID *renderGuid, const DSCBUFFERDESC *dscBufferDesc, const DSBUFFERDESC *dsBufferDesc, HWND hwnd, DWORD level, IDirectSoundCaptureBuffer8 **dsCaptureBuffer8, IDirectSoundBuffer8 **dsBuffer8) noexcept override;

    void finalize() noexcept
    {
        if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1) [[unlikely]]
            delete this;
    }

    template<typename T> [[nodiscard]]
    T as() noexcept { return static_cast<T>(this); }

    static ComPtr<DSFullDuplex> Create();
};

#endif // DSFULLDUPLEX_H
