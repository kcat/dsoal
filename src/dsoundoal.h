#ifndef DSOUNDOAL_H
#define DSOUNDOAL_H

#include <atomic>
#include <dsound.h>
#include <memory>
#include <mutex>
#include <vector>

#include "AL/alc.h"

#include "comptr.h"
#include "primarybuffer.h"


struct SharedDevice {
    struct NoDeleter {
        template<typename T>
        void operator()(T*) noexcept { }
    };

    SharedDevice() = default;
    SharedDevice(const SharedDevice&) = delete;
    SharedDevice(SharedDevice&& rhs) = default;
    ~SharedDevice();

    SharedDevice& operator=(const SharedDevice&) = delete;
    SharedDevice& operator=(SharedDevice&&) = delete;

    GUID mId{};
    DWORD mSpeakerConfig{};

    DWORD mMaxHwSources{};
    DWORD mMaxSwSources{};

    std::unique_ptr<ALCdevice,NoDeleter> mDevice;
    std::unique_ptr<ALCcontext,NoDeleter> mContext;

    size_t mUseCount{1u};
};


class DSound8OAL final : IDirectSound8, IDirectSound {
    DSound8OAL(bool is8);
    ~DSound8OAL();

    static std::mutex sDeviceListMutex;
    static std::vector<std::unique_ptr<SharedDevice>> sDeviceList;

    class UnknownImpl final : IUnknown {
        DSound8OAL *impl_from_base() noexcept
        {
#ifdef __GNUC__
    _Pragma("GCC diagnostic push")
    _Pragma("GCC diagnostic ignored \"-Wcast-align\"")
#endif
            return CONTAINING_RECORD(this, DSound8OAL, mUnknownIface);
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
    DWORD mPrioLevel{};

    SharedDevice *mShared{};

    PrimaryBuffer mPrimaryBuffer;

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

    [[nodiscard]]
    DWORD getPriorityLevel() const noexcept { return mPrioLevel; }

    template<typename T>
    T as() noexcept { return static_cast<T>(this); }

    static ComPtr<DSound8OAL> Create(bool is8);
};

#endif // DSOUNDOAL_H
