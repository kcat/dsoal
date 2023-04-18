#ifndef DSOUNDOAL_H
#define DSOUNDOAL_H

#include <algorithm>
#include <atomic>
#include <bitset>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

#include <dsound.h>

#include "AL/alc.h"
#include "comptr.h"
#include "dsoal.h"
#include "expected.h"
#include "primarybuffer.h"


class Buffer;

enum Extensions : uint8_t {
    EXT_EAX,
    EXT_FLOAT32,
    EXT_STATIC_BUFFER,

    ExtensionCount
};

struct SharedDevice {
    static std::mutex sDeviceListMutex;
    static std::vector<std::unique_ptr<SharedDevice>> sDeviceList;

    struct NoDeleter {
        template<typename T>
        void operator()(T*) noexcept { }
    };

    SharedDevice(const GUID &id) : mId{id} { }
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
    std::atomic<DWORD> mCurrentHwSources{};
    std::atomic<DWORD> mCurrentSwSources{};

    std::bitset<ExtensionCount> mExtensions;

    const GUID mId;
    DWORD mSpeakerConfig{};

    std::unique_ptr<ALCdevice,NoDeleter> mDevice;
    std::unique_ptr<ALCcontext,NoDeleter> mContext;

    std::atomic<ULONG> mRef{1u};

    DWORD getCurrentHwCount() const noexcept
    { return mCurrentHwSources.load(std::memory_order_relaxed); }

    DWORD getCurrentSwCount() const noexcept
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


/* Secondary buffers are preallocated in groups of 64. This allows them to be
 * constructed and destructed in place without having to continually allocate
 * and deallocate them individually.
 */
class BufferSubList {
public:
    uint64_t mFreeMask{~0_u64};
    Buffer *mBuffers{nullptr}; /* 64 */

    BufferSubList() noexcept = default;
    BufferSubList(const BufferSubList&) = delete;
    BufferSubList(BufferSubList&& rhs) noexcept : mFreeMask{rhs.mFreeMask}, mBuffers{rhs.mBuffers}
    { rhs.mFreeMask = ~0_u64; rhs.mBuffers = nullptr; }
    ~BufferSubList();

    BufferSubList& operator=(const BufferSubList&) = delete;
    BufferSubList& operator=(BufferSubList&& rhs) noexcept
    { std::swap(mFreeMask, rhs.mFreeMask); std::swap(mBuffers, rhs.mBuffers); return *this; }
};


class DSound8OAL final : IDirectSound8 {
    DSound8OAL(bool is8);
    ~DSound8OAL();

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

    std::mutex mDsMutex;
    DWORD mPrioLevel{};

    ComPtr<SharedDevice> mShared;

    std::bitset<ExtensionCount> mExtensions;

    std::vector<BufferSubList> mSecondaryBuffers;
    PrimaryBuffer mPrimaryBuffer;

    std::vector<Buffer*> m3dBuffers;

    bool mIs8{};

    ComPtr<Buffer> createSecondaryBuffer(IDirectSoundBuffer *original=nullptr);

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

    void dispose(Buffer *buffer) noexcept;

    void add3dBuffer(Buffer *buffer)
    { m3dBuffers.emplace_back(buffer); }

    void remove3dBuffer(Buffer *buffer)
    {
        auto iter = std::remove(m3dBuffers.begin(), m3dBuffers.end(), buffer);
        m3dBuffers.erase(iter, m3dBuffers.end());
    }

    [[nodiscard]]
    std::mutex &getMutex() noexcept { return mDsMutex; }

    [[nodiscard]]
    bool haveExtension(Extensions flag) const noexcept { return mExtensions.test(flag); }

    [[nodiscard]]
    std::bitset<ExtensionCount> getExtensions() const noexcept { return mExtensions; }

    [[nodiscard]]
    DWORD getPriorityLevel() const noexcept { return mPrioLevel; }

    [[nodiscard]]
    SharedDevice &getShared() noexcept { return *mShared; }

    [[nodiscard]]
    PrimaryBuffer &getPrimary() noexcept { return mPrimaryBuffer; }

    [[nodiscard]]
    std::vector<Buffer*> &get3dBuffers() noexcept { return m3dBuffers; }

    template<typename T> [[nodiscard]]
    T as() noexcept
    {
        /* MinGW headers do not have IDirectSound8 inherit from IDirectSound,
         * which MSVC apparently does. IDirectSound is a strict subset of
         * IDirectSound8, so the interface is ABI compatible.
         */
        if constexpr(std::is_same_v<T,IDirectSound*>
            && !std::is_base_of_v<IDirectSound,DSound8OAL>)
            return ds::bit_cast<T>(static_cast<IDirectSound8*>(this));
        else
            return static_cast<T>(this);
    }

    [[nodiscard]]
    static ComPtr<DSound8OAL> Create(bool is8);
};

#endif // DSOUNDOAL_H
