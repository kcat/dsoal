#ifndef DSOUNDOAL_H
#define DSOUNDOAL_H

#include <atomic>
#include <dsound.h>
#include <memory>
#include <mutex>
#include <vector>

#include "AL/alc.h"
#include "comptr.h"


struct SharedDevice {
    SharedDevice() = default;
    SharedDevice(SharedDevice&& rhs)
        : mId{rhs.mId}, mSpeakerConfig{rhs.mSpeakerConfig}, mDevice{rhs.mDevice}
        , mContext{rhs.mContext}, mUseCount{rhs.mUseCount}
    {
        rhs.mDevice = nullptr;
        rhs.mContext = nullptr;
        rhs.mUseCount = 0;
    }
    ~SharedDevice();

    SharedDevice& operator=(SharedDevice&& rhs)
    {
        std::swap(mId, rhs.mId);
        std::swap(mSpeakerConfig, rhs.mSpeakerConfig);
        std::swap(mDevice, rhs.mDevice);
        std::swap(mContext, rhs.mContext);
        std::swap(mUseCount, rhs.mUseCount);
        return *this;
    }

    GUID mId{};
    DWORD mSpeakerConfig{};

    ALCdevice *mDevice{};
    ALCcontext *mContext{};

    size_t mUseCount{1u};
};


class DSound8OAL final : IDirectSound8, IDirectSound {
    DSound8OAL(bool is8);
    ~DSound8OAL();

    static std::mutex sDeviceListMutex;
    static std::vector<std::unique_ptr<SharedDevice>> sDeviceList;

    std::atomic<ULONG> mRef{1u};

    bool mIs8{};

    SharedDevice *mShared{};

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
    HRESULT STDMETHODCALLTYPE Initialize(const GUID *lpcGuid) noexcept override;
    HRESULT STDMETHODCALLTYPE VerifyCertification(DWORD *certified) noexcept override;

    template<typename T>
    T as() noexcept { return static_cast<T>(this); }

    static ComPtr<DSound8OAL> Create(bool is8);
};

#endif // DSOUNDOAL_H
