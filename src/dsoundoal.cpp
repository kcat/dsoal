#include "dsoundoal.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>

#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include "buffer.h"
#include "comhelpers.h"
#include "comptr.h"
#include "dsoal.h"
#include "enumerate.h"
#include "expected.h"
#include "fmt/chrono.h"
#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

using SubListAllocator = std::allocator<std::array<Buffer,64>>;

using namespace std::string_view_literals;

constexpr auto CapNames = std::array{
    "DSBCAPS_PRIMARYBUFFER"sv,      // 0x00000001
    "DSBCAPS_STATIC"sv,             // 0x00000002
    "DSBCAPS_LOCHARDWARE"sv,        // 0x00000004
    "DSBCAPS_LOCSOFTWARE"sv,        // 0x00000008
    "DSBCAPS_CTRL3D"sv,             // 0x00000010
    "DSBCAPS_CTRLFREQUENCY"sv,      // 0x00000020
    "DSBCAPS_CTRLPAN"sv,            // 0x00000040
    "DSBCAPS_CTRLVOLUME"sv,         // 0x00000080
    "DSBCAPS_CTRLPOSITIONNOTIFY"sv, // 0x00000100
    "DSBCAPS_CTRLFX"sv,             // 0x00000200
    "0x400"sv,                      // 0x00000400
    "0x800"sv,                      // 0x00000800
    "0x1000"sv,                     // 0x00001000
    "0x2000"sv,                     // 0x00002000
    "DSBCAPS_STICKYFOCUS"sv,        // 0x00004000
    "DSBCAPS_GLOBALFOCUS"sv,        // 0x00008000
    "DSBCAPS_GETCURRENTPOSITION2"sv,// 0x00010000
    "DSBCAPS_MUTE3DATMAXDISTANCE"sv,// 0x00020000
    "DSBCAPS_LOCDEFER"sv,           // 0x00040000
    "DSBCAPS_TRUEPLAYPOSITION"sv,   // 0x00080000
};

auto GetDSBCapsString(DWORD flags) -> std::string
{
    auto ret = std::string{};
    ret.reserve(256);

    for(const auto idx : std::views::iota(size_t{0}, CapNames.size()))
    {
        const auto flag = DWORD{1} << idx;
        if(flag > flags)
            break;
        if(!(flags&flag))
            continue;
        flags &= ~flag;

        if(!ret.empty())
            ret += " | ";
        ret += CapNames[idx];
    }

    if(ret.empty() || flags != 0)
    {
        if(!ret.empty())
            ret += " | ";
        ret += fmt::format("{:#x}", flags);
    }

    return ret;
}


template<typename T>
using CoTaskMemPtr = std::unique_ptr<T, decltype([](T *mem) { CoTaskMemFree(mem); })>;

using ALCdevicePtr = std::unique_ptr<ALCdevice, decltype([](ALCdevice *device)
    { alcCloseDevice(device); })>;

using ALCcontextPtr = std::unique_ptr<ALCcontext, decltype([](ALCcontext *context)
    { alcDestroyContext(context); })>;


#define PREFIX "GetSpeakerConfig "
std::optional<DWORD> GetSpeakerConfig(IMMDevice *device)
{
    ComPtr<IPropertyStore> ps;
    HRESULT hr{device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps))};
    if(FAILED(hr))
    {
        WARN("IMMDevice::OpenPropertyStore failed: {:08x}", as_unsigned(hr));
        return std::nullopt;
    }

    DWORD speakerconf{DSSPEAKER_7POINT1_SURROUND};

    PropVariant pv;
    hr = ps->GetValue(PKEY_AudioEndpoint_PhysicalSpeakers, pv.get());
    if(FAILED(hr))
    {
        WARN("IPropertyStore::GetValue(PhysicalSpeakers) failed: {:08x}", as_unsigned(hr));
        return speakerconf;
    }
    if(pv.type() != VT_UI4 && pv.type() != VT_UINT)
    {
        WARN("PhysicalSpeakers is not a VT_UI4: 0x{:04x}", pv.type());
        return speakerconf;
    }

    const auto phys_speakers = pv.value<ULONG>();
    auto match_speakers = [phys_speakers](const ULONG speakers) noexcept
    { return (phys_speakers&speakers) == speakers; };

    if(match_speakers(KSAUDIO_SPEAKER_7POINT1))
        speakerconf = DSSPEAKER_7POINT1;
    else if(match_speakers(KSAUDIO_SPEAKER_7POINT1_SURROUND))
        speakerconf = DSSPEAKER_7POINT1_SURROUND;
    else if(match_speakers(KSAUDIO_SPEAKER_5POINT1))
        speakerconf = DSSPEAKER_5POINT1_BACK;
    else if(match_speakers(KSAUDIO_SPEAKER_5POINT1_SURROUND))
        speakerconf = DSSPEAKER_5POINT1_SURROUND;
    else if(match_speakers(KSAUDIO_SPEAKER_QUAD))
        speakerconf = DSSPEAKER_QUAD;
    else if(match_speakers(KSAUDIO_SPEAKER_STEREO))
        speakerconf = DSSPEAKER_COMBINED(DSSPEAKER_STEREO, DSSPEAKER_GEOMETRY_WIDE);
    else if(match_speakers(KSAUDIO_SPEAKER_MONO))
        speakerconf = DSSPEAKER_MONO;
    else
    {
        FIXME("Unhandled physical speaker layout: 0x{:08x}", phys_speakers);
        return speakerconf;
    }

    if(DSSPEAKER_CONFIG(speakerconf) == DSSPEAKER_STEREO)
    {
        pv.clear();
        hr = ps->GetValue(PKEY_AudioEndpoint_FormFactor, pv.get());
        if(FAILED(hr))
            WARN("IPropertyStore::GetValue(FormFactor) failed: {:08x}", as_unsigned(hr));
        else if(pv.type() != VT_UI4 && pv.type() != VT_UINT)
            WARN("FormFactor is not a VT_UI4: 0x{:04x}", pv.type());
        else if(pv.value<UINT>() == Headphones || pv.value<UINT>() == Headset)
            speakerconf = DSSPEAKER_HEADPHONE;
    }

    TRACE("Got config {}:{} from physical speakers 0x{:08x}", DSSPEAKER_GEOMETRY(speakerconf),
        DSSPEAKER_CONFIG(speakerconf), phys_speakers);

    return speakerconf;
}
#undef PREFIX

#define PREFIX "CreateDeviceShare "
ds::expected<std::unique_ptr<SharedDevice>,HRESULT> CreateDeviceShare(const GUID &guid)
{
    TRACE("Creating shared device {}", GuidPrinter{guid}.c_str());

    DWORD speakerconf{DSSPEAKER_7POINT1_SURROUND};

    ComWrapper com;
    auto device = GetMMDevice(com, eRender, guid);
    if(auto config = GetSpeakerConfig(device.get()))
        speakerconf = *config;
    device = nullptr;

    const auto drv_name = std::invoke([&guid]() -> std::string
    {
        try {
            auto guid_str = CoTaskMemPtr<OLECHAR>{};
            const auto hr = StringFromCLSID(guid, ds::out_ptr(guid_str));
            if(SUCCEEDED(hr)) return wstr_to_utf8(guid_str.get());

            ERR("StringFromCLSID failed: {:#x}", as_unsigned(hr));
        }
        catch(std::exception &e) {
            ERR("Exception converting GUID to string: {}", e.what());
        }
        return std::string{};
    });
    if(drv_name.empty())
        return ds::unexpected(DSERR_OUTOFMEMORY);

    ALCdevicePtr aldev{alcOpenDevice(drv_name.c_str())};
    if(!aldev)
    {
        WARN("Couldn't open device \"{}\", 0x{:04x}", drv_name, alcGetError(nullptr));
        return ds::unexpected(DSERR_NODRIVER);
    }

    auto get_name = [&aldev]() -> const char*
    {
        if(alcIsExtensionPresent(aldev.get(), "ALC_ENUMERATE_ALL_EXT"))
            return alcGetString(aldev.get(), ALC_ALL_DEVICES_SPECIFIER);
        return alcGetString(aldev.get(), ALC_DEVICE_SPECIFIER);
    };
    TRACE("Opened AL device: {}", get_name());

    const std::array attrs{
        ALint{ALC_MONO_SOURCES}, ALint{MaxSources},
        ALint{ALC_STEREO_SOURCES}, 0,
        0
    };
    ALCcontextPtr alctx{alcCreateContext(aldev.get(), attrs.data())};
    if(!alctx)
    {
        WARN("Couldn't create context, 0x{:04x}", alcGetError(aldev.get()));
        return ds::unexpected(DSERR_NODRIVER);
    }

    struct ExtensionEntry {
        const char *name;
        Extensions flag;
    };
    const std::array sExtensionList{
        ExtensionEntry{"EAX5.0", EXT_EAX},
        ExtensionEntry{"ALC_EXT_EFX", EXT_EFX},
        ExtensionEntry{"AL_EXT_FLOAT32", EXT_FLOAT32},
        ExtensionEntry{"AL_EXT_MCFORMATS", EXT_MCFORMATS},
        ExtensionEntry{"AL_EXT_STATIC_BUFFER", EXT_STATIC_BUFFER},
        ExtensionEntry{"AL_SOFT_source_spatialize", SOFT_SOURCE_SPATIALIZE},
        ExtensionEntry{"AL_SOFTX_source_panning", SOFT_SOURCE_PANNING},
    };

    std::bitset<ExtensionCount> extensions{};
    for(auto &ext : sExtensionList)
    {
        if(std::string_view{ext.name}.substr(0,3) == "ALC")
        {
            if(alcIsExtensionPresent(aldev.get(), ext.name))
            {
                extensions.set(ext.flag);
                TRACE("Found extension {}", ext.name);
            }
        }
        else if(alIsExtensionPresentDirect(alctx.get(), ext.name))
        {
            extensions.set(ext.flag);
            TRACE("Found extension {}", ext.name);
        }
    }

    /* TODO: Could also support AL_SOFTX_map_buffer, for older OpenAL Soft
     * versions, or AL_SOFT_callback_buffer.
     */
    if(!extensions.test(EXT_STATIC_BUFFER))
    {
        WARN("Missing the required AL_EXT_STATIC_BUFFER extension");
        return ds::unexpected(DSERR_NODRIVER);
    }

    ALCint numMono{}, numStereo{};
    alcGetIntegerv(aldev.get(), ALC_MONO_SOURCES, 1, &numMono);
    alcGetIntegerv(aldev.get(), ALC_STEREO_SOURCES, 1, &numStereo);
    alcGetError(aldev.get());

    numMono = std::max(numMono, 0);
    numStereo = std::max(numStereo, 0);
    const DWORD totalSources{static_cast<DWORD>(numMono) + static_cast<DWORD>(numStereo)};
    if(totalSources < 128)
    {
        ERR("Could only allocate {} sources (minimum 128 required)", totalSources);
        return ds::unexpected(DSERR_OUTOFMEMORY);
    }

    const DWORD maxHw{totalSources > MaxHwSources*2 ? MaxHwSources : (MaxHwSources/2)};

    auto refresh = ALCint{20};
    alcGetIntegerv(aldev.get(), ALC_REFRESH, 1, &refresh);
    alcGetError(aldev.get());

    /* Restrict the update period to between 10ms and 50ms (100hz and 20hz
     * update rate).
     */
    refresh = std::clamp(refresh, 20, 100);

    auto shared = std::make_unique<SharedDevice>(guid);
    shared->mSpeakerConfig = speakerconf;
    shared->mMaxHwSources = maxHw;
    shared->mMaxSwSources = totalSources - maxHw;
    shared->mExtensions = extensions;
    shared->mRefresh = static_cast<ALCuint>(refresh);
    shared->mDevice = aldev.release();
    shared->mContext = alctx.release();

    return shared;
}
#undef PREFIX

} // namespace

#define CLASS_PREFIX "SharedDevice::"
std::mutex SharedDevice::sDeviceListMutex;
std::vector<SharedDevice*> SharedDevice::sDeviceList;

auto SharedDevice::GetById(const GUID &deviceId) noexcept
    -> ds::expected<ComPtr<SharedDevice>,HRESULT>
{
    std::unique_lock listlock{sDeviceListMutex};
    auto sharediter = std::ranges::find(sDeviceList, deviceId, &SharedDevice::mId);
    if(sharediter != sDeviceList.end())
    {
        (*sharediter)->AddRef();
        return ComPtr<SharedDevice>{*sharediter};
    }

    auto shared = CreateDeviceShare(deviceId);
    if(!shared) return ds::unexpected(shared.error());

    sDeviceList.emplace_back();
    sDeviceList.back() = shared->release();
    return ComPtr<SharedDevice>{sDeviceList.back()};
}

SharedDevice::~SharedDevice()
{
    if(mContext)
        alcDestroyContext(mContext);
    if(mDevice)
        alcCloseDevice(mDevice);
}

#define PREFIX CLASS_PREFIX "dispose "
void SharedDevice::dispose() noexcept
{
    std::lock_guard listlock{sDeviceListMutex};

    auto shared_iter = std::ranges::find(sDeviceList, this);
    if(shared_iter != sDeviceList.end())
    {
        std::unique_ptr<SharedDevice> device{*shared_iter};
        TRACE("Freeing shared device {}", GuidPrinter{device->mId}.c_str());
        sDeviceList.erase(shared_iter);
    }
}
#undef PREFIX
#undef CLASS_PREFIX

BufferSubList::~BufferSubList()
{
    if(!mBuffers)
        return;

    uint64_t usemask{~mFreeMask};
    while(usemask)
    {
        auto idx = ds::countr_zero(usemask);
        std::destroy_at(std::to_address(mBuffers->begin() + idx));
        usemask &= ~(1_u64 << idx);
    }
    SubListAllocator{}.deallocate(mBuffers, 1);
}


#define CLASS_PREFIX "DSound8OAL::"
ComPtr<DSound8OAL> DSound8OAL::Create(bool is8)
{
    return ComPtr<DSound8OAL>{new DSound8OAL{is8}};
}

DSound8OAL::DSound8OAL(bool is8) : mPrimaryBuffer{*this}, mIs8{is8}
{
}

DSound8OAL::~DSound8OAL()
{
    if(mNotifyThread.joinable())
    {
        mQuitNotify = true;
        /* Temporarily lock the mutex to ensure we're not between checking
         * mQuitNotify and before waiting on mNotifyCond.
         */
        { std::unique_lock _{mDsMutex}; }
        mNotifyCond.notify_all();
        mNotifyThread.join();
    }
}


ComPtr<Buffer> DSound8OAL::createSecondaryBuffer(IDirectSoundBuffer *original)
{
    std::unique_lock lock{mDsMutex};
    BufferSubList *sublist{nullptr};
    /* Find a group with an available buffer. */
    for(auto &group : mSecondaryBuffers)
    {
        if(group.mFreeMask)
        {
            sublist = &group;
            break;
        }
    }
    if(!sublist) [[unlikely]]
    {
        /* If none are available, make another group. */
        BufferSubList group;
        group.mBuffers = SubListAllocator{}.allocate(1);
        if(group.mBuffers)
        {
            mSecondaryBuffers.emplace_back(std::move(group));
            sublist = &mSecondaryBuffers.back();
        }
    }
    if(!sublist)
        return {};

    auto idx = static_cast<unsigned int>(ds::countr_zero(sublist->mFreeMask));
    ComPtr<Buffer> buffer{::new(&(*sublist->mBuffers)[idx]) Buffer{*this, mIs8, original}};
    sublist->mFreeMask &= ~(1_u64 << idx);

    return buffer;
}


#define PREFIX CLASS_PREFIX "notifyThread "
void DSound8OAL::notifyThread() noexcept
{
    using namespace std::chrono;

    /* Calculate the wait time to be 3/5ths the time between refreshes. This
     * causes about two wakeups per OpenAL update, but helps ensure
     * notifications respond within half an update period.
     */
    const auto waittime = milliseconds{seconds{1}} / mRefresh * 3 / 5;

    TRACE("Wakeup every {}", waittime);

    std::unique_lock lock{mDsMutex};
    while(!mQuitNotify)
    {
        if(mNotifyBuffers.empty())
        {
            mNotifyCond.wait(lock);
            continue;
        }

        auto enditer = std::ranges::remove_if(mNotifyBuffers, [](Buffer *buffer) noexcept
        { return !buffer->updateNotify(); });
        mNotifyBuffers.erase(enditer.begin(), enditer.end());

        mNotifyCond.wait_for(lock, waittime);
    }
}
#undef PREFIX

void DSound8OAL::triggerNotifies() noexcept
{
    auto enditer = std::ranges::remove_if(mNotifyBuffers, [](Buffer *buffer) noexcept
    { return !buffer->updateNotify(); });
    mNotifyBuffers.erase(enditer.begin(), enditer.end());
}

void DSound8OAL::addNotifyBuffer(Buffer *buffer)
{
    if(std::ranges::find(mNotifyBuffers, buffer) == mNotifyBuffers.end())
    {
        mNotifyBuffers.emplace_back(buffer);
        if(!mNotifyThread.joinable()) [[unlikely]]
            mNotifyThread = std::thread{&DSound8OAL::notifyThread, this};
        else if(mNotifyBuffers.size() == 1)
            mNotifyCond.notify_all();
    }
}


#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE DSound8OAL::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    if(!ppvObject)
        return E_POINTER;
    *ppvObject = nullptr;

    if(riid == IID_IUnknown)
    {
        mUnknownIface.AddRef();
        *ppvObject = mUnknownIface.as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IDirectSound8)
    {
        if(!mIs8) [[unlikely]]
        {
            WARN("Requesting IDirectSound8 iface for non-DS8 object");
            return E_NOINTERFACE;
        }
        AddRef();
        *ppvObject = as<IDirectSound8*>();
        return S_OK;
    }
    if(riid == IID_IDirectSound)
    {
        AddRef();
        *ppvObject = as<IDirectSound*>();
        return S_OK;
    }

    FIXME("Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSound8OAL::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSound8OAL::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    finalize();
    return ret;
}
#undef PREFIX


#define PREFIX CLASS_PREFIX "CreateSoundBuffer "
HRESULT STDMETHODCALLTYPE DSound8OAL::CreateSoundBuffer(const DSBUFFERDESC *bufferDesc,
    IDirectSoundBuffer **dsBuffer, IUnknown *outer) noexcept
{
    DEBUG("({})->({}, {}, {})", voidp{this}, cvoidp{bufferDesc}, voidp{dsBuffer}, voidp{outer});

    if(!dsBuffer)
    {
        WARN("dsBuffer is null");
        return DSERR_INVALIDPARAM;
    }
    *dsBuffer = nullptr;

    if(outer)
    {
        WARN("Aggregation isn't supported");
        return DSERR_NOAGGREGATION;
    }
    if(!bufferDesc || bufferDesc->dwSize < sizeof(DSBUFFERDESC1))
    {
        WARN("Invalid DSBUFFERDESC ({}, {})", cvoidp{bufferDesc},
            bufferDesc ? bufferDesc->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    if(!mShared)
    {
        WARN("Device not initialized");
        return DSERR_UNINITIALIZED;
    }

    DSBUFFERDESC bufdesc{};
    std::memcpy(&bufdesc, bufferDesc, std::min<DWORD>(sizeof(bufdesc), bufferDesc->dwSize));
    bufdesc.dwSize = std::min<DWORD>(sizeof(bufdesc), bufferDesc->dwSize);

    if(bufdesc.dwSize >= sizeof(DSBUFFERDESC))
    {
        TRACE("Requested buffer:\n"
            "    Size        = {}\n"
            "    Flags       = {}\n"
            "    BufferBytes = {}\n"
            "    3DAlgorithm = {}",
            bufdesc.dwSize, GetDSBCapsString(bufdesc.dwFlags), bufdesc.dwBufferBytes,
            Ds3dalgPrinter{bufdesc.guid3DAlgorithm}.c_str());
    }
    else
    {
        TRACE("Requested buffer:\n"
            "    Size        = {}\n"
            "    Flags       = {}\n"
            "    BufferBytes = {}",
            bufdesc.dwSize, GetDSBCapsString(bufdesc.dwFlags), bufdesc.dwBufferBytes);
    }

    HRESULT hr{E_FAIL};
    if((bufdesc.dwFlags&DSBCAPS_PRIMARYBUFFER))
    {
        /* NOTE: Primary buffers seem to allow simultaneous 3D and panning
         * control. Presumably because it's not a positional playback buffer
         * itself (outside of WRITEPRIMARY) and just modifies secondary buffers
         * as appropriate.
         */
        hr = DS_OK;
        if(mPrimaryBuffer.AddRef() == 1 && mPrimaryBuffer.getFlags() == 0)
        {
            hr = mPrimaryBuffer.Initialize(as<IDirectSound*>(), &bufdesc);
            if(FAILED(hr))
                mPrimaryBuffer.Release();
        }
        if(SUCCEEDED(hr))
            *dsBuffer = mPrimaryBuffer.as<IDirectSoundBuffer*>();
    }
    else
    {
        /* OpenAL doesn't support playing with 3D and panning at same time. */
        if((bufdesc.dwFlags&(DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN)) == (DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN))
        {
            /* Neither does DirectSound 8. */
            if(mIs8)
            {
                WARN("Cannot create secondary buffers with 3D and pan control");
                return DSERR_INVALIDPARAM;
            }

            /* DS7 does, though. No idea what it expects to happen. */
            if(static bool once{false}; !once)
            {
                once = true;
                FIXME("Secondary buffers with 3D and pan control ignore panning");
            }
        }

        ComPtr<Buffer> buffer{createSecondaryBuffer()};
        hr = buffer->Initialize(as<IDirectSound*>(), &bufdesc);
        if(SUCCEEDED(hr))
            *dsBuffer = buffer.release()->as<IDirectSoundBuffer*>();
    }

    return hr;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetCaps "
HRESULT STDMETHODCALLTYPE DSound8OAL::GetCaps(DSCAPS *dsCaps) noexcept
{
    TRACE("({})->({})", voidp{this}, voidp{dsCaps});

    if(!mShared)
    {
        WARN("Device not initialized");
        return DSERR_UNINITIALIZED;
    }

    if(!dsCaps || dsCaps->dwSize < sizeof(*dsCaps))
    {
        WARN("Invalid DSCAPS ({}, {})", voidp{dsCaps}, dsCaps ? dsCaps->dwSize : 0lu);
        return DSERR_INVALIDPARAM;
    }

    dsCaps->dwFlags = DSCAPS_CONTINUOUSRATE | DSCAPS_CERTIFIED | DSCAPS_PRIMARY16BIT
        | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARYMONO | DSCAPS_SECONDARY16BIT
        | DSCAPS_SECONDARY8BIT | DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO;
    dsCaps->dwPrimaryBuffers = 1;
    dsCaps->dwMinSecondarySampleRate = DSBFREQUENCY_MIN;
    dsCaps->dwMaxSecondarySampleRate = DSBFREQUENCY_MAX;
    dsCaps->dwMaxHwMixingAllBuffers =
        dsCaps->dwMaxHwMixingStaticBuffers =
        dsCaps->dwMaxHwMixingStreamingBuffers =
        dsCaps->dwMaxHw3DAllBuffers =
        dsCaps->dwMaxHw3DStaticBuffers =
        dsCaps->dwMaxHw3DStreamingBuffers = mShared->mMaxHwSources;
    dsCaps->dwFreeHwMixingAllBuffers =
        dsCaps->dwFreeHwMixingStaticBuffers =
        dsCaps->dwFreeHwMixingStreamingBuffers =
        dsCaps->dwFreeHw3DAllBuffers =
        dsCaps->dwFreeHw3DStaticBuffers =
        dsCaps->dwFreeHw3DStreamingBuffers = mShared->mMaxHwSources - mShared->getCurrentHwCount();
    dsCaps->dwTotalHwMemBytes =
        dsCaps->dwFreeHwMemBytes = 64 * 1024 * 1024;
    dsCaps->dwMaxContigFreeHwMemBytes = dsCaps->dwFreeHwMemBytes;
    dsCaps->dwUnlockTransferRateHwBuffers = 4096;
    dsCaps->dwPlayCpuOverheadSwBuffers = 0;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "DuplicateSoundBuffer "
HRESULT STDMETHODCALLTYPE DSound8OAL::DuplicateSoundBuffer(IDirectSoundBuffer *original,
    IDirectSoundBuffer **duplicate) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, voidp{original}, voidp{duplicate});

    if(!mShared)
    {
        WARN("Device not initialized");
        return DSERR_UNINITIALIZED;
    }

    if(!original || !duplicate)
    {
        WARN("Invalid pointer: in = {}, out = {}", voidp{original}, voidp{duplicate});
        return DSERR_INVALIDPARAM;
    }
    *duplicate = nullptr;

    DSBCAPS caps{};
    caps.dwSize = sizeof(caps);
    HRESULT hr{original->GetCaps(&caps)};
    if(FAILED(hr))
    {
        WARN("Failed to get caps for buffer {}", voidp{original});
        return DSERR_INVALIDPARAM;
    }
    if((caps.dwFlags&DSBCAPS_PRIMARYBUFFER))
    {
        WARN("Cannot duplicate primary buffer {}", voidp{original});
        return DSERR_INVALIDPARAM;
    }
    if((caps.dwFlags&DSBCAPS_CTRLFX))
    {
        WARN("Cannot duplicate buffer {}, which has DSBCAPS_CTRLFX", voidp{original});
        return DSERR_INVALIDPARAM;
    }

    auto buffer = createSecondaryBuffer(original);
    if(!buffer) return DSERR_OUTOFMEMORY;

    hr = buffer->Initialize(as<IDirectSound*>(), nullptr);
    if(FAILED(hr)) return hr;

    *duplicate = buffer.release()->as<IDirectSoundBuffer*>();
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetCooperativeLevel "
HRESULT STDMETHODCALLTYPE DSound8OAL::SetCooperativeLevel(HWND hwnd, DWORD level) noexcept
{
    TRACE("({})->({}, {})", voidp{this}, voidp{hwnd}, level);

    if(!mShared)
    {
        WARN("Device not initialized");
        return DSERR_UNINITIALIZED;
    }

    if(level > DSSCL_WRITEPRIMARY || level < DSSCL_NORMAL)
    {
        WARN("Invalid cooperative level: {}", level);
        return DSERR_INVALIDPARAM;
    }

    std::lock_guard lock{mDsMutex};
    auto hr = S_OK;
    if(level == DSSCL_WRITEPRIMARY && mPrioLevel != DSSCL_WRITEPRIMARY)
    {
        mPrimaryBuffer.destroyWriteEmu();

        for(auto &group : mSecondaryBuffers)
        {
            auto usemask = ~group.mFreeMask;
            while(usemask)
            {
                auto idx = static_cast<unsigned int>(ds::countr_zero(usemask));
                usemask &= ~(1_u64 << idx);

                auto state = DWORD{};
                auto &buffer = (*group.mBuffers)[idx];
                hr = buffer.GetStatus(&state);
                if(FAILED(hr) || (state&DSBSTATUS_PLAYING))
                {
                    WARN("DSSCL_WRITEPRIMARY set with playing buffers!");
                    return DSERR_INVALIDCALL;
                }
                buffer.setLost();
            }
        }

        if(const DWORD flags{mPrimaryBuffer.getFlags()})
            hr = mPrimaryBuffer.createWriteEmu(flags);
    }
    else if(level < DSSCL_WRITEPRIMARY && mPrioLevel == DSSCL_WRITEPRIMARY)
    {
        TRACE("Nuking mWriteEmu");
        mPrimaryBuffer.destroyWriteEmu();
    }
    if(SUCCEEDED(hr))
        mPrioLevel = level;

    return hr;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Compact "
HRESULT STDMETHODCALLTYPE DSound8OAL::Compact() noexcept
{
    DEBUG("({})->()", voidp{this});

    if(!mShared)
    {
        WARN("Device not initialized");
        return DSERR_UNINITIALIZED;
    }

    if(mPrioLevel < DSSCL_PRIORITY)
    {
        WARN("Cooperative level too low: {}", mPrioLevel);
        return DSERR_PRIOLEVELNEEDED;
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetSpeakerConfig "
HRESULT STDMETHODCALLTYPE DSound8OAL::GetSpeakerConfig(DWORD *speakerConfig) noexcept
{
    TRACE("({})->({})", voidp{this}, voidp{speakerConfig});

    if(!speakerConfig)
        return DSERR_INVALIDPARAM;
    *speakerConfig = 0;

    if(!mShared)
    {
        WARN("Device not initialized");
        return DSERR_UNINITIALIZED;
    }

    *speakerConfig = mShared->mSpeakerConfig;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetSpeakerConfig "
HRESULT STDMETHODCALLTYPE DSound8OAL::SetSpeakerConfig(DWORD speakerConfig) noexcept
{
    TRACE("({})->(0x{:08x})", voidp{this}, speakerConfig);

    if(!mShared)
    {
        WARN("Device not initialized");
        return DSERR_UNINITIALIZED;
    }

    const DWORD geo{DSSPEAKER_GEOMETRY(speakerConfig)};
    const DWORD speaker{DSSPEAKER_CONFIG(speakerConfig)};

    if(geo && (geo < DSSPEAKER_GEOMETRY_MIN || geo > DSSPEAKER_GEOMETRY_MAX))
    {
        WARN("Invalid speaker angle {}", geo);
        return DSERR_INVALIDPARAM;
    }
    if(speaker < DSSPEAKER_HEADPHONE || speaker > DSSPEAKER_5POINT1_SURROUND)
    {
        WARN("Invalid speaker config {}", speaker);
        return DSERR_INVALIDPARAM;
    }

    /* No-op on Vista+. */
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Initialize "
HRESULT STDMETHODCALLTYPE DSound8OAL::Initialize(const GUID *deviceId) noexcept
{
    TRACE("({})->({})", voidp{this}, DevidPrinter{deviceId}.c_str());

    if(mShared)
    {
        WARN("Device already initialized");
        return DSERR_ALREADYINITIALIZED;
    }

    if(!deviceId || *deviceId == GUID_NULL)
        deviceId = &DSDEVID_DefaultPlayback;
    else if(*deviceId == DSDEVID_DefaultCapture || *deviceId == DSDEVID_DefaultVoiceCapture)
        return DSERR_NODRIVER;

    GUID devid{};
    HRESULT hr{GetDeviceID(*deviceId, devid)};
    if(FAILED(hr)) return hr;

    auto shared = SharedDevice::GetById(devid);
    if(!shared) return shared.error();
    mShared = std::move(shared).value();

    mPrimaryBuffer.setContext(mShared->mContext);
    mExtensions = mShared->mExtensions;
    mRefresh = mShared->mRefresh;

    /* Preallocate some groups for the number of "hardware" buffers we can do.
     * This will grow as needed.
     */
    const size_t numGroups{(mShared->mMaxHwSources+63) / 64};
    for(size_t i{0};i < numGroups;++i)
    {
        BufferSubList group;
        group.mBuffers = SubListAllocator{}.allocate(1);
        if(!group.mBuffers) break;

        mSecondaryBuffers.emplace_back(std::move(group));
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "VerifyCertification "
HRESULT STDMETHODCALLTYPE DSound8OAL::VerifyCertification(DWORD *certified) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{certified});

    if(!certified)
        return DSERR_INVALIDPARAM;
    *certified = 0;

    if(!mShared)
    {
        WARN("Device not initialized");
        return DSERR_UNINITIALIZED;
    }

    *certified = DS_CERTIFIED;

    return DS_OK;
}
#undef PREFIX

void DSound8OAL::dispose(Buffer *buffer) noexcept
{
    std::lock_guard lock{mDsMutex};
    /* Find the group the given buffer belongs in, then destruct it and mark it
     * as free.
     */
    for(auto &group : mSecondaryBuffers)
    {
        ptrdiff_t idx{buffer - group.mBuffers->data()};
        if(static_cast<std::make_unsigned_t<ptrdiff_t>>(idx) < 64)
        {
            std::destroy_at(buffer);
            group.mFreeMask |= 1_u64 << idx;
            return;
        }
    }
}
#undef CLASS_PREFIX

#define CLASS_PREFIX "DSound8OAL::Unknown::"
HRESULT STDMETHODCALLTYPE DSound8OAL::Unknown::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSound8OAL::Unknown::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mUnkRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSound8OAL::Unknown::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mUnkRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    self->finalize();
    return ret;
}
#undef PREFIX
#undef CLASS_PREFIX
