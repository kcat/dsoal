#ifndef PRIMARYBUFFER_H
#define PRIMARYBUFFER_H

#include <atomic>
#include <bitset>
#include <memory>
#include <mutex>

#include <dsound.h>
#include <mmreg.h>

#include "AL/alc.h"


class DSound8OAL;
class Buffer;

class PrimaryBuffer final : IDirectSoundBuffer {
    class Listener3D final : IDirectSound3DListener {
        auto impl_from_base() noexcept
        {
#ifdef __GNUC__
    _Pragma("GCC diagnostic push")
    _Pragma("GCC diagnostic ignored \"-Wcast-align\"")
#endif
            return CONTAINING_RECORD(this, PrimaryBuffer, mListener3D);
#ifdef __GNUC__
    _Pragma("GCC diagnostic pop")
#endif
        }

    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
        ULONG STDMETHODCALLTYPE AddRef() noexcept override;
        ULONG STDMETHODCALLTYPE Release() noexcept override;
        HRESULT STDMETHODCALLTYPE GetAllParameters(DS3DLISTENER *listener) noexcept override;
        HRESULT STDMETHODCALLTYPE GetDistanceFactor(D3DVALUE *distanceFactor) noexcept override;
        HRESULT STDMETHODCALLTYPE GetDopplerFactor(D3DVALUE *dopplerFactor) noexcept override;
        HRESULT STDMETHODCALLTYPE GetOrientation(D3DVECTOR *orientFront, D3DVECTOR *orientTop) noexcept override;
        HRESULT STDMETHODCALLTYPE GetPosition(D3DVECTOR *position) noexcept override;
        HRESULT STDMETHODCALLTYPE GetRolloffFactor(D3DVALUE *rolloffFactor) noexcept override;
        HRESULT STDMETHODCALLTYPE GetVelocity(D3DVECTOR *velocity) noexcept override;
        HRESULT STDMETHODCALLTYPE SetAllParameters(const DS3DLISTENER *listener, DWORD apply) noexcept override;
        HRESULT STDMETHODCALLTYPE SetDistanceFactor(D3DVALUE distanceFactor, DWORD apply) noexcept override;
        HRESULT STDMETHODCALLTYPE SetDopplerFactor(D3DVALUE dopplerFactor, DWORD apply) noexcept override;
        HRESULT STDMETHODCALLTYPE SetOrientation(D3DVALUE xFront, D3DVALUE yFront, D3DVALUE zFront, D3DVALUE xTop, D3DVALUE yTop, D3DVALUE zTop, DWORD apply) noexcept override;
        HRESULT STDMETHODCALLTYPE SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept override;
        HRESULT STDMETHODCALLTYPE SetRolloffFactor(D3DVALUE rolloffFactor, DWORD apply) noexcept override;
        HRESULT STDMETHODCALLTYPE SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept override;
        HRESULT STDMETHODCALLTYPE CommitDeferredSettings() noexcept override;

        Listener3D() = default;
        Listener3D(const Listener3D&) = delete;
        Listener3D& operator=(const Listener3D&) = delete;

        template<typename T>
        T as() noexcept { return static_cast<T>(this); }
    };
    Listener3D mListener3D;

    class Prop final : IKsPropertySet {
        auto impl_from_base() noexcept
        {
#ifdef __GNUC__
            _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wcast-align\"")
#endif
            return CONTAINING_RECORD(this, PrimaryBuffer, mProp);
#ifdef __GNUC__
            _Pragma("GCC diagnostic pop")
#endif
        }

    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
        ULONG STDMETHODCALLTYPE AddRef() noexcept override;
        ULONG STDMETHODCALLTYPE Release() noexcept override;
        HRESULT STDMETHODCALLTYPE Get(REFGUID guidPropSet, ULONG dwPropID, void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData, ULONG *pcbReturned) noexcept override;
        HRESULT STDMETHODCALLTYPE Set(REFGUID guidPropSet, ULONG dwPropID, void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData) noexcept override;
        HRESULT STDMETHODCALLTYPE QuerySupport(REFGUID guidPropSet, ULONG dwPropID, ULONG *pTypeSupport) noexcept override;

        Prop() = default;
        Prop(const Prop&) = delete;
        Prop& operator=(const Prop&) = delete;

        template<typename T>
        T as() noexcept { return static_cast<T>(this); }
    };
    Prop mProp;

    std::atomic<ULONG> mTotalRef{0u}, mDsRef{0u}, mDs3dRef{0u}, mPropRef{0u};

    DWORD mFlags{0u};

    DSound8OAL &mParent;
    ALCcontext *mContext{};

    std::recursive_mutex &mMutex;
    LONG mVolume{};
    LONG mPan{};

    DS3DLISTENER mImmediate{};
    DS3DLISTENER mDeferred{};
    enum DirtyFlags {
        Position,
        Velocity,
        Orientation,
        DistanceFactor,
        RolloffFactor,
        DopplerFactor,

        FlagCount
    };
    std::bitset<FlagCount> mDirty;

    std::unique_ptr<Buffer> mWriteEmu;
    WAVEFORMATEXTENSIBLE mFormat{};
    bool mPlaying{false};

    void setParams(const DS3DLISTENER &params, const std::bitset<FlagCount> flags);

public:
    PrimaryBuffer() = delete;
    PrimaryBuffer(const PrimaryBuffer&) = delete;
    explicit PrimaryBuffer(DSound8OAL &parent);
    ~PrimaryBuffer();

    PrimaryBuffer& operator=(const PrimaryBuffer&) = delete;

    /*** IUnknown methods ***/
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    /*** IDirectSoundBuffer methods ***/
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

    void commit() noexcept;

    [[nodiscard]]
    float getCurrentRolloffFactor() const noexcept { return mImmediate.flRolloffFactor; }

    void setContext(ALCcontext *context) noexcept
    { mContext = context; }

    [[nodiscard]]
    auto getWriteEmu() const noexcept -> Buffer* { return mWriteEmu.get(); }

    [[nodiscard]]
    auto getFlags() const noexcept -> DWORD { return mFlags; }

    auto createWriteEmu(DWORD flags) noexcept -> HRESULT;

    void destroyWriteEmu() noexcept;

    template<typename T>
    T as() noexcept { return static_cast<T>(this); }
};

#endif // PRIMARYBUFFER_H
