#ifndef BUFFER_H
#define BUFFER_H

#include <atomic>
#include <memory>

#include <dsound.h>
#include <mmreg.h>

#include "AL/al.h"
#include "comptr.h"
#include "dsoal.h"
#include "expected.h"


struct DSound8OAL;


class SharedBuffer {
    std::atomic<ULONG> mRef{1u};

    auto dispose() noexcept -> void;

public:
    SharedBuffer() = default;
    ~SharedBuffer();

    auto AddRef() noexcept -> ULONG { return mRef.fetch_add(1u, std::memory_order_relaxed)+1; }
    auto Release() noexcept -> ULONG
    {
        const auto ret = mRef.fetch_sub(1u, std::memory_order_relaxed)-1;
        if(ret == 0) dispose();
        return ret;
    }

    char *mData;
    DWORD mDataSize{0};
    DWORD mFlags{};

    WAVEFORMATEXTENSIBLE mWfxFormat{};
    ALenum mAlFormat{AL_NONE};
    ALuint mAlBuffer{0};

    static auto Create(const DSBUFFERDESC &bufferDesc) noexcept
        -> ds::expected<ComPtr<SharedBuffer>,HRESULT>;
};

class Buffer final : IDirectSoundBuffer8 {
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

    DSound8OAL &mParent;
    ALCcontext *mContext{};

    ComPtr<SharedBuffer> mBuffer;
    ALuint mSource{};

    bool mIsHardware{false};
    bool mIs8{};
    bool mIsInitialized{false};

public:
    Buffer(DSound8OAL &parent, bool is8);
    ~Buffer();

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

#ifdef __MINGW32__
/* MinGW headers do not have IDirectSoundBuffer8 inherit from
 * IDirectSoundBuffer, which MSVC apparently does. IDirectSoundBuffer is a
 * subset of IDirectSoundBuffer8, so it should be ABI-compatible.
 */
template<>
inline IDirectSoundBuffer *Buffer::as() noexcept
{ return ds::bit_cast<IDirectSoundBuffer*>(static_cast<IDirectSoundBuffer8*>(this)); }
#endif

#endif // BUFFER_H
