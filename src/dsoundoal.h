#ifndef DSOUNDOAL_H
#define DSOUNDOAL_H

#include <algorithm>
#include <atomic>
#include <bit>
#include <bitset>
#include <condition_variable>
#include <mutex>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

#include <dsound.h>

#include "AL/alc.h"
#include "comptr.h"
#include "expected.h"
#include "primarybuffer.h"


class Buffer;
struct BufferSubList;

enum Extensions : uint8_t {
    EXT_EAX,
    EXT_EFX,
    EXT_FLOAT32,
    EXT_MCFORMATS,
    EXT_STATIC_BUFFER,
    SOFT_SOURCE_PANNING,

    ExtensionCount
};

struct SharedDevice {
    static std::mutex sDeviceListMutex;
    static std::vector<SharedDevice*> sDeviceList;

    explicit SharedDevice(const GUID &id) : mId{id} { }
    SharedDevice(const SharedDevice&) = delete;
    ~SharedDevice();

    SharedDevice& operator=(const SharedDevice&) = delete;

    void dispose() noexcept;

    ULONG AddRef() noexcept { return mRef.fetch_add(1u, std::memory_order_relaxed)+1; }
    ULONG Release() noexcept
    {
        const auto ret = mRef.fetch_sub(1u, std::memory_order_relaxed)-1;
        if(ret == 0) dispose();
        return ret;
    }

    DWORD mMaxHwSources{};
    DWORD mMaxSwSources{};
    std::atomic<DWORD> mCurrentHwSources;
    std::atomic<DWORD> mCurrentSwSources;

    std::bitset<ExtensionCount> mExtensions;
    DWORD mRefresh{100u};

    const GUID mId;
    DWORD mSpeakerConfig{};

    ALCdevice *mDevice{};
    ALCcontext *mContext{};

    std::atomic<ULONG> mRef{1u};

    [[nodiscard]]
    auto getCurrentHwCount() const noexcept -> DWORD
    { return mCurrentHwSources.load(std::memory_order_relaxed); }

    [[nodiscard]]
    auto getCurrentSwCount() const noexcept -> DWORD
    { return mCurrentSwSources.load(std::memory_order_relaxed); }

    /* Increment mCurrentHwSources up to mMaxHwSources. Returns false is the
     * current is already at max, or true if it was successfully incremented.
     */
    bool incHwSources() noexcept
    {
        DWORD cur{mCurrentHwSources.load(std::memory_order_relaxed)};
        do {
            if(cur == mMaxHwSources)
                return false;
        } while(!mCurrentHwSources.compare_exchange_weak(cur, cur+1, std::memory_order_relaxed));
        return true;
    }

    /* Increment mCurrentSwSources up to mMaxSwSources. */
    bool incSwSources() noexcept
    {
        DWORD cur{mCurrentSwSources.load(std::memory_order_relaxed)};
        do {
            if(cur == mMaxSwSources)
                return false;
        } while(!mCurrentSwSources.compare_exchange_weak(cur, cur+1, std::memory_order_relaxed));
        return true;
    }

    void decHwSources() noexcept { mCurrentHwSources.fetch_sub(1u, std::memory_order_relaxed); }
    void decSwSources() noexcept { mCurrentSwSources.fetch_sub(1u, std::memory_order_relaxed); }

    static auto GetById(const GUID &deviceId) noexcept -> ds::expected<ComPtr<SharedDevice>,HRESULT>;
};


class DSound8OAL final : IDirectSound8 {
    explicit DSound8OAL(bool is8);
    ~DSound8OAL();

    class Unknown final : IUnknown {
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

        Unknown() = default;
        Unknown(const Unknown&) = delete;
        Unknown& operator=(const Unknown&) = delete;

        template<typename T>
        T as() noexcept { return static_cast<T>(this); }
    };
    Unknown mUnknownIface;

    std::atomic<ULONG> mTotalRef{1u}, mDsRef{1u}, mUnkRef{0u};

    std::recursive_mutex mDsMutex;
    DWORD mPrioLevel{};

    ComPtr<SharedDevice> mShared;

    std::bitset<ExtensionCount> mExtensions;
    DWORD mRefresh{100u};

    std::vector<BufferSubList> mSecondaryBuffers;
    PrimaryBuffer mPrimaryBuffer;

    std::vector<Buffer*> m3dBuffers;
    std::vector<Buffer*> mNotifyBuffers;

    std::condition_variable_any mNotifyCond;
    std::thread mNotifyThread;

    std::atomic<bool> mQuitNotify{false};
    bool mIs8{};

    ComPtr<Buffer> createSecondaryBuffer(IDirectSoundBuffer *original=nullptr);

    void notifyThread() noexcept;

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

    void finalize() noexcept
    {
        if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1) [[unlikely]]
            delete this;
    }

    void dispose(Buffer *buffer) noexcept;

    void add3dBuffer(Buffer *buffer)
    { m3dBuffers.emplace_back(buffer); }

    void remove3dBuffer(Buffer *buffer)
    {
        auto iter = std::remove(m3dBuffers.begin(), m3dBuffers.end(), buffer);
        m3dBuffers.erase(iter, m3dBuffers.end());
    }

    void addNotifyBuffer(Buffer *buffer);

    void removeNotifyBuffer(Buffer *buffer)
    {
        auto iter = std::remove(mNotifyBuffers.begin(), mNotifyBuffers.end(), buffer);
        mNotifyBuffers.erase(iter, mNotifyBuffers.end());
    }

    [[nodiscard]]
    bool isPendingNotify(Buffer *buffer) const noexcept
    {
        const auto iter = std::find(mNotifyBuffers.cbegin(), mNotifyBuffers.cend(), buffer);
        return iter != mNotifyBuffers.cend();
    }

    void triggerNotifies() noexcept;

    [[nodiscard]]
    std::vector<BufferSubList> &getSecondaryBuffers() noexcept { return mSecondaryBuffers; }

    [[nodiscard]]
    auto getMutex() noexcept -> std::recursive_mutex& { return mDsMutex; }

    [[nodiscard]]
    bool haveExtension(Extensions flag) const noexcept { return mExtensions.test(flag); }

    [[nodiscard]]
    std::bitset<ExtensionCount> getExtensions() const noexcept { return mExtensions; }

    [[nodiscard]]
    auto getRefresh() const noexcept { return mRefresh; }

    [[nodiscard]]
    DWORD getPriorityLevel() const noexcept { return mPrioLevel; }

    [[nodiscard]]
    SharedDevice &getShared() noexcept { return *mShared; }

    [[nodiscard]]
    PrimaryBuffer &getPrimary() noexcept { return mPrimaryBuffer; }

    [[nodiscard]]
    auto get3dBuffers() noexcept -> std::span<Buffer*> { return m3dBuffers; }

    template<typename T> [[nodiscard]]
    T as() noexcept
    {
        /* MinGW headers do not have IDirectSound8 inherit from IDirectSound,
         * which MSVC apparently does. IDirectSound is a strict subset of
         * IDirectSound8, so the interface is ABI compatible.
         */
        if constexpr(std::is_same_v<T,IDirectSound*>
            && !std::is_base_of_v<IDirectSound,DSound8OAL>)
            return std::bit_cast<T>(static_cast<IDirectSound8*>(this));
        else
            return static_cast<T>(this);
    }

    [[nodiscard]]
    static ComPtr<DSound8OAL> Create(bool is8);
};

#endif // DSOUNDOAL_H
