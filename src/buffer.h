#ifndef BUFFER_H
#define BUFFER_H

#include <atomic>

#include <dsound.h>


class Buffer final : IDirectSoundBuffer8, IDirectSoundBuffer {
    class UnknownImpl final : IUnknown {
        Buffer *impl_from_base() noexcept
        {
#ifdef __GNUC__
    _Pragma("GCC diagnostic push")
    _Pragma("GCC diagnostic ignored \"-Wcast-align\"")
#endif
            return CONTAINING_RECORD(this, Buffer, mUnknownIface);
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
    UnknownImpl mUnknownIface;

    std::atomic<ULONG> mTotalRef{1u}, mDsRef{1u}, mUnkRef{0u};

    bool mIs8{};

public:
    Buffer(bool is8);

    /*** IUnknown methods ***/
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    /*** IDirectSoundBuffer8 methods ***/
    HRESULT STDMETHODCALLTYPE GetCaps(DSBCAPS *bufferCaps) noexcept override;
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(DWORD *playCursor, DWORD *writeCursor) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFormat(WAVEFORMATEX *wfx, DWORD sizeAllocated, DWORD *sizeWritten) noexcept override;
    HRESULT STDMETHODCALLTYPE GetVolume(LONG *volume) noexcept override;
    HRESULT STDMETHODCALLTYPE GetPan(LONG *pan) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFrequency(DWORD *frequency) noexcept override;
    HRESULT STDMETHODCALLTYPE GetStatus(DWORD *status) noexcept override;
    HRESULT STDMETHODCALLTYPE Initialize(IDirectSound *directSound, const DSBUFFERDESC *dsBufferDesc) noexcept override;
    HRESULT STDMETHODCALLTYPE Lock(DWORD offset, DWORD bytes, void **audioPtr1, DWORD *audioBytes1, void **audioPtr2, DWORD *audioBytes2, DWORD flags) noexcept override;
    HRESULT STDMETHODCALLTYPE Play(DWORD reserved1, DWORD reserved2, DWORD flags) noexcept override;
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD newPosition) noexcept override;
    HRESULT STDMETHODCALLTYPE SetFormat(const WAVEFORMATEX *wfx) noexcept override;
    HRESULT STDMETHODCALLTYPE SetVolume(LONG volume) noexcept override;
    HRESULT STDMETHODCALLTYPE SetPan(LONG pan) noexcept override;
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD frequency) noexcept override;
    HRESULT STDMETHODCALLTYPE Stop() noexcept override;
    HRESULT STDMETHODCALLTYPE Unlock(void *audioPtr1, DWORD audioBytes1, void *audioPtr2, DWORD audioBytes2) noexcept override;
    HRESULT STDMETHODCALLTYPE Restore() noexcept override;
    HRESULT STDMETHODCALLTYPE SetFX(DWORD dwEffectsCount, DSEFFECTDESC *dsFXDesc, DWORD *resultCodes) noexcept override;
    HRESULT STDMETHODCALLTYPE AcquireResources(DWORD flags, DWORD effectsCount, DWORD *resultCodes) noexcept override;
    HRESULT STDMETHODCALLTYPE GetObjectInPath(REFGUID objectId, DWORD index, REFGUID interfaceId, void **ppObject) noexcept override;

    template<typename T>
    T as() noexcept { return static_cast<T>(this); }
};

#endif // BUFFER_H
