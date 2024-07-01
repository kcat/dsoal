#include "dsoundoal.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <functional>
#include <memory>
#include <optional>

#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include "buffer.h"
#include "comhelpers.h"
#include "comptr.h"
#include "dsoal.h"
#include "enumerate.h"
#include "expected.h"
#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

using SubListAllocator = std::allocator<std::array<Buffer,64>>;


struct ALCdeviceDeleter {
    void operator()(ALCdevice *device) { alcCloseDevice(device); }
};
using ALCdevicePtr = std::unique_ptr<ALCdevice,ALCdeviceDeleter>;

struct ALCcontextDeleter {
    void operator()(ALCcontext *context)
    {
        if(context == alcGetThreadContext())
            alcSetThreadContext(nullptr);
        alcDestroyContext(context);
    }
};
using ALCcontextPtr = std::unique_ptr<ALCcontext,ALCcontextDeleter>;


auto wstr_to_utf8(std::wstring_view wstr) -> std::string
{
    std::string ret;

    const int len{WideCharToMultiByte(CP_UTF8, 0, wstr.data(), ds::sizei(wstr), nullptr, 0,
        nullptr, nullptr)};
    if(len > 0)
    {
        ret.resize(static_cast<size_t>(len));
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), ds::sizei(wstr), ret.data(), len,
            nullptr, nullptr);
    }

    return ret;
}

std::optional<DWORD> GetSpeakerConfig(IMMDevice *device)
{
    ComPtr<IPropertyStore> ps;
    HRESULT hr{device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps))};
    if(FAILED(hr))
    {
        WARN("GetSpeakerConfig IMMDevice::OpenPropertyStore failed: %08lx\n", hr);
        return std::nullopt;
    }

    DWORD speakerconf{DSSPEAKER_7POINT1_SURROUND};

    PropVariant pv;
    hr = ps->GetValue(PKEY_AudioEndpoint_PhysicalSpeakers, pv.get());
    if(FAILED(hr))
    {
        WARN("GetSpeakerConfig IPropertyStore::GetValue(PhysicalSpeakers) failed: %08lx\n", hr);
        return speakerconf;
    }
    if(pv.type() != VT_UI4 || pv.type() != VT_UINT)
    {
        WARN("GetSpeakerConfig PhysicalSpeakers is not a VT_UI4: 0x%04x\n", pv.type());
        return speakerconf;
    }

    const auto phys_speakers = pv.value<ULONG>();
    pv.clear();

#define BIT_MATCH(v, b) (((v)&(b)) == (b))
    if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_7POINT1))
        speakerconf = DSSPEAKER_7POINT1;
    else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_7POINT1_SURROUND))
        speakerconf = DSSPEAKER_7POINT1_SURROUND;
    else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_5POINT1))
        speakerconf = DSSPEAKER_5POINT1_BACK;
    else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_5POINT1_SURROUND))
        speakerconf = DSSPEAKER_5POINT1_SURROUND;
    else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_QUAD))
        speakerconf = DSSPEAKER_QUAD;
    else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_STEREO))
        speakerconf = DSSPEAKER_COMBINED(DSSPEAKER_STEREO, DSSPEAKER_GEOMETRY_WIDE);
    else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_MONO))
        speakerconf = DSSPEAKER_MONO;
    else
    {
        FIXME("GetSpeakerConfig Unhandled physical speaker layout: 0x%08lx\n",
            phys_speakers);
        return speakerconf;
    }
#undef BIT_MATCH

    if(DSSPEAKER_CONFIG(speakerconf) == DSSPEAKER_STEREO)
    {
        hr = ps->GetValue(PKEY_AudioEndpoint_FormFactor, pv.get());
        if(FAILED(hr))
            WARN("GetSpeakerConfig IPropertyStore::GetValue(FormFactor) failed: %08lx\n", hr);
        else if(pv.type() != VT_UI4 || pv.type() != VT_UINT)
            WARN("GetSpeakerConfig FormFactor is not a VT_UI4: 0x%04x\n", pv.type());
        else if(pv.value<UINT>() == Headphones || pv.value<UINT>() == Headset)
            speakerconf = DSSPEAKER_HEADPHONE;
    }

    TRACE("GetSpeakerConfig Got config %d:%d from physical speakers 0x%08lx\n",
        DSSPEAKER_GEOMETRY(speakerconf), DSSPEAKER_CONFIG(speakerconf), phys_speakers);

    return speakerconf;
}

ds::expected<std::unique_ptr<SharedDevice>,HRESULT> CreateDeviceShare(const GUID &guid)
{
    TRACE("CreateDeviceShare Creating shared device %s\n", GuidPrinter{guid}.c_str());

    DWORD speakerconf{DSSPEAKER_7POINT1_SURROUND};

    ComWrapper com;
    auto device = GetMMDevice(com, eRender, guid);
    if(auto config = GetSpeakerConfig(device.get()))
        speakerconf = *config;
    device = nullptr;

    std::string drv_name;
    {
        LPOLESTR guid_str{};
        HRESULT hr{StringFromCLSID(guid, &guid_str)};
        if(FAILED(hr))
        {
            ERR("CreateDeviceShare Failed to convert GUID to string\n");
            return ds::unexpected(hr);
        }
        drv_name = wstr_to_utf8(guid_str);
        CoTaskMemFree(guid_str);
        guid_str = nullptr;
    }

    HRESULT hr{DSERR_NODRIVER};
    ALCdevicePtr aldev{alcOpenDevice(drv_name.c_str())};
    if(!aldev)
    {
        WARN("CreateDeviceShare Couldn't open device \"%s\", 0x%04x\n", drv_name.c_str(),
            alcGetError(nullptr));
        return ds::unexpected(hr);
    }
    TRACE("CreateDeviceShare Opened AL device: %s\n",
        alcIsExtensionPresent(aldev.get(), "ALC_ENUMERATE_ALL_EXT") ?
        alcGetString(aldev.get(), ALC_ALL_DEVICES_SPECIFIER) :
        alcGetString(aldev.get(), ALC_DEVICE_SPECIFIER));

    const std::array attrs{
        ALint{ALC_MONO_SOURCES}, ALint{MaxSources},
        ALint{ALC_STEREO_SOURCES}, 0,
        0
    };
    ALCcontextPtr alctx{alcCreateContext(aldev.get(), attrs.data())};
    if(!alctx)
    {
        WARN("CreateDeviceShare Couldn't create context, 0x%04x\n", alcGetError(aldev.get()));
        return ds::unexpected(hr);
    }
    ALSection alsection{alctx.get()};

    struct ExtensionEntry {
        const char *name;
        Extensions flag;
    };
    const std::array sExtensionList{
        ExtensionEntry{"EAX5.0", EXT_EAX},
        ExtensionEntry{"AL_EXT_FLOAT32", EXT_FLOAT32},
        ExtensionEntry{"AL_EXT_STATIC_BUFFER", EXT_STATIC_BUFFER}
    };

    std::bitset<ExtensionCount> extensions{};
    for(auto &ext : sExtensionList)
    {
        if(alIsExtensionPresent(ext.name))
        {
            extensions.set(ext.flag);
            TRACE("CreateDeviceShare Found extension %s\n", ext.name);
        }
    }

    /* TODO: Could also support AL_SOFTX_map_buffer, for older OpenAL Soft
     * versions, or AL_SOFT_callback_buffer.
     */
    if(!extensions.test(EXT_STATIC_BUFFER))
    {
        WARN("CreateDeviceShare Missing the required AL_EXT_STATIC_BUFFER extension\n");
        return ds::unexpected(hr);
    }

    ALCint numMono{}, numStereo{};
    alcGetIntegerv(aldev.get(), ALC_MONO_SOURCES, 1, &numMono);
    alcGetIntegerv(aldev.get(), ALC_STEREO_SOURCES, 1, &numStereo);
    alcGetError(aldev.get());

    if(numMono < 0) numMono = 0;
    if(numStereo < 0) numStereo = 0;
    const DWORD totalSources{static_cast<DWORD>(numMono) + static_cast<DWORD>(numStereo)};
    if(totalSources < 128)
    {
        ERR("CreateDeviceShare Could only allocate %lu sources (minimum 128 required)\n",
            totalSources);
        return ds::unexpected(DSERR_OUTOFMEMORY);
    }

    const DWORD maxHw{totalSources > MaxHwSources*2 ? MaxHwSources : (MaxHwSources/2)};

    auto shared = std::make_unique<SharedDevice>(guid);
    shared->mSpeakerConfig = speakerconf;
    shared->mMaxHwSources = maxHw;
    shared->mMaxSwSources = totalSources - maxHw;
    shared->mExtensions = extensions;
    shared->mDevice = aldev.release();
    shared->mContext = alctx.release();

    return shared;
}

} // namespace

#define CLASS_PREFIX "SharedDevice::"
std::mutex SharedDevice::sDeviceListMutex;
std::vector<SharedDevice*> SharedDevice::sDeviceList;

auto SharedDevice::GetById(const GUID &deviceId) noexcept
    -> ds::expected<ComPtr<SharedDevice>,HRESULT>
{
    auto find_id = [&deviceId](SharedDevice *device)
    { return deviceId == device->mId; };

    std::unique_lock listlock{sDeviceListMutex};
    auto sharediter = std::find_if(sDeviceList.begin(), sDeviceList.end(), find_id);
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
    {
        if(mContext == alcGetThreadContext())
            alcSetThreadContext(nullptr);
        alcDestroyContext(mContext);
    }
    if(mDevice)
        alcCloseDevice(mDevice);
}

#define PREFIX CLASS_PREFIX "dispose "
void SharedDevice::dispose() noexcept
{
    std::lock_guard listlock{sDeviceListMutex};

    auto shared_iter = std::find(sDeviceList.begin(), sDeviceList.end(), this);
    if(shared_iter != sDeviceList.end())
    {
        std::unique_ptr<SharedDevice> device{*shared_iter};
        TRACE(PREFIX "Freeing shared device %s\n", GuidPrinter{device->mId}.c_str());
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
        auto idx = int{ds::countr_zero(usemask)};
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
    if(!sublist) UNLIKELY
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
    alcSetThreadContext(mShared->mContext);

    ALCint refresh{};
    alcGetIntegerv(mShared->mDevice, ALC_REFRESH, 1, &refresh);

    using namespace std::chrono;
    milliseconds waittime{10};
    if(refresh > 0)
    {
        /* Calculate the wait time to be 3/5ths the time between refreshes.
         * This causes about two wakeups per OpenAL update, but helps ensure
         * notifications respond within half an update period.
         */
        waittime = milliseconds{seconds{1}} / refresh;
        waittime = std::max(waittime*3/5, milliseconds{10});
    }
    TRACE(PREFIX "Wakeup every %" PRId64 "ms\n",
        int64_t{duration_cast<milliseconds>(waittime).count()});

    std::unique_lock lock{mDsMutex};
    while(!mQuitNotify)
    {
        if(mNotifyBuffers.empty())
        {
            mNotifyCond.wait(lock);
            continue;
        }

        auto enditer = std::remove_if(mNotifyBuffers.begin(), mNotifyBuffers.end(),
            [](Buffer *buffer) noexcept { return !buffer->updateNotify(); });
        mNotifyBuffers.erase(enditer, mNotifyBuffers.end());

        mNotifyCond.wait_for(lock, waittime);
    }

    alcSetThreadContext(nullptr);
}
#undef PREFIX

void DSound8OAL::triggerNotifies() noexcept
{
    auto enditer = std::remove_if(mNotifyBuffers.begin(), mNotifyBuffers.end(),
        [](Buffer *buffer) noexcept { return !buffer->updateNotify(); });
    mNotifyBuffers.erase(enditer, mNotifyBuffers.end());
}

void DSound8OAL::addNotifyBuffer(Buffer *buffer)
{
    if(std::find(mNotifyBuffers.cbegin(), mNotifyBuffers.cend(), buffer) == mNotifyBuffers.cend())
    {
        mNotifyBuffers.emplace_back(buffer);
        if(!mNotifyThread.joinable()) UNLIKELY
            mNotifyThread = std::thread{&DSound8OAL::notifyThread, this};
        else if(mNotifyBuffers.size() == 1)
            mNotifyCond.notify_all();
    }
}


#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE DSound8OAL::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG(PREFIX "(%p)->(%s, %p)\n", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    *ppvObject = nullptr;
    if(riid == IID_IUnknown)
    {
        mUnknownIface.AddRef();
        *ppvObject = mUnknownIface.as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IDirectSound8)
    {
        if(!mIs8) UNLIKELY
        {
            WARN(PREFIX "Requesting IDirectSound8 iface for non-DS8 object\n");
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

    FIXME(PREFIX "Unhandled GUID: %s\n", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

ULONG STDMETHODCALLTYPE DSound8OAL::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(CLASS_PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSound8OAL::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(CLASS_PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        delete this;
    return ret;
}


#define PREFIX CLASS_PREFIX "CreateSoundBuffer "
HRESULT STDMETHODCALLTYPE DSound8OAL::CreateSoundBuffer(const DSBUFFERDESC *bufferDesc,
    IDirectSoundBuffer **dsBuffer, IUnknown *outer) noexcept
{
    DEBUG(PREFIX "(%p)->(%p, %p, %p)\n", voidp{this}, cvoidp{bufferDesc}, voidp{dsBuffer},
        voidp{outer});

    if(!dsBuffer)
    {
        WARN(PREFIX "dsBuffer is null\n");
        return DSERR_INVALIDPARAM;
    }
    *dsBuffer = nullptr;

    if(outer)
    {
        WARN(PREFIX "Aggregation isn't supported\n");
        return DSERR_NOAGGREGATION;
    }
    if(!bufferDesc || bufferDesc->dwSize < sizeof(DSBUFFERDESC1))
    {
        WARN(PREFIX "Invalid DSBUFFERDESC (%p, %lu)\n", cvoidp{bufferDesc},
            bufferDesc ? bufferDesc->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    if(!mShared)
    {
        WARN(PREFIX "Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    DSBUFFERDESC bufdesc{};
    std::memcpy(&bufdesc, bufferDesc, std::min<DWORD>(sizeof(bufdesc), bufferDesc->dwSize));
    bufdesc.dwSize = std::min<DWORD>(sizeof(bufdesc), bufferDesc->dwSize);

    if(bufdesc.dwSize >= sizeof(DSBUFFERDESC))
    {
        TRACE(PREFIX "Requested buffer:\n"
              "    Size        = %lu\n"
              "    Flags       = 0x%08lx\n"
              "    BufferBytes = %lu\n"
              "    3DAlgorithm = %s\n",
            bufdesc.dwSize, bufdesc.dwFlags, bufdesc.dwBufferBytes,
            Ds3dalgPrinter{bufdesc.guid3DAlgorithm}.c_str());
    }
    else
    {
        TRACE(PREFIX "Requested buffer:\n"
              "    Size        = %lu\n"
              "    Flags       = 0x%08lx\n"
              "    BufferBytes = %lu\n",
            bufdesc.dwSize, bufdesc.dwFlags, bufdesc.dwBufferBytes);
    }

    /* OpenAL doesn't support playing with 3d and panning at same time. */
    if((bufdesc.dwFlags&(DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN)) == (DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN))
    {
        /* Neither does DirectSound 8. */
        if(mIs8)
        {
            WARN(PREFIX "Cannot create buffers with 3D and pan control\n");
            return DSERR_INVALIDPARAM;
        }

        /* DS7 does, though. No idea what it expects to happen. */
        static int once{0};
        if(!once)
        {
            ++once;
            FIXME(PREFIX "Buffers with 3D and pan control ignore panning\n");
        }
    }

    HRESULT hr{E_FAIL};
    if((bufdesc.dwFlags&DSBCAPS_PRIMARYBUFFER))
    {
        hr = DS_OK;
        if(mPrimaryBuffer.AddRef() == 1)
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
    DEBUG(PREFIX "(%p)->(%p)\n", voidp{this}, voidp{dsCaps});

    if(!mShared)
    {
        WARN(PREFIX "Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(!dsCaps || dsCaps->dwSize < sizeof(*dsCaps))
    {
        WARN(PREFIX "Invalid DSCAPS (%p, %lu)\n", voidp{dsCaps}, dsCaps ? dsCaps->dwSize : 0lu);
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
    DEBUG(PREFIX "(%p)->(%p, %p)\n", voidp{this}, voidp{original}, voidp{duplicate});

    if(!mShared)
    {
        WARN(PREFIX "Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(!original || !duplicate)
    {
        WARN(PREFIX "Invalid pointer: in = %p, out = %p\n", voidp{original}, voidp{duplicate});
        return DSERR_INVALIDPARAM;
    }
    *duplicate = nullptr;

    DSBCAPS caps{};
    caps.dwSize = sizeof(caps);
    HRESULT hr{original->GetCaps(&caps)};
    if(FAILED(hr))
    {
        WARN(PREFIX "Failed to get caps for buffer %p\n", voidp{original});
        return DSERR_INVALIDPARAM;
    }
    if((caps.dwFlags&DSBCAPS_PRIMARYBUFFER))
    {
        WARN(PREFIX "Cannot duplicate primary buffer %p\n", voidp{original});
        return DSERR_INVALIDPARAM;
    }
    if((caps.dwFlags&DSBCAPS_CTRLFX))
    {
        WARN(PREFIX "Cannot duplicate buffer %p, which has DSBCAPS_CTRLFX\n", voidp{original});
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
    DEBUG(PREFIX "(%p)->(%p, %lu)\n", voidp{this}, voidp{hwnd}, level);

    if(!mShared)
    {
        WARN(PREFIX "Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(level > DSSCL_WRITEPRIMARY || level < DSSCL_NORMAL)
    {
        WARN(PREFIX "Invalid cooperative level: %lu\n", level);
        return DSERR_INVALIDPARAM;
    }

    if(level == DSSCL_WRITEPRIMARY)
    {
        FIXME(PREFIX "DSSCL_WRITEPRIMARY not currently supported\n");
        return DSERR_INVALIDPARAM;
    }

    mPrioLevel = level;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Compact "
HRESULT STDMETHODCALLTYPE DSound8OAL::Compact() noexcept
{
    DEBUG(PREFIX "(%p)->()\n", voidp{this});

    if(!mShared)
    {
        WARN(PREFIX "Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(mPrioLevel < DSSCL_PRIORITY)
    {
        WARN("Cooperative level too low: %lu\n", mPrioLevel);
        return DSERR_PRIOLEVELNEEDED;
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetSpeakerConfig "
HRESULT STDMETHODCALLTYPE DSound8OAL::GetSpeakerConfig(DWORD *speakerConfig) noexcept
{
    DEBUG(PREFIX "(%p)->(%p)\n", voidp{this}, voidp{speakerConfig});

    if(!speakerConfig)
        return DSERR_INVALIDPARAM;
    *speakerConfig = 0;

    if(!mShared)
    {
        WARN(PREFIX "Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    *speakerConfig = mShared->mSpeakerConfig;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetSpeakerConfig "
HRESULT STDMETHODCALLTYPE DSound8OAL::SetSpeakerConfig(DWORD speakerConfig) noexcept
{
    DEBUG(PREFIX "(%p)->(0x%08lx)\n", voidp{this}, speakerConfig);

    if(!mShared)
    {
        WARN(PREFIX "Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    const DWORD geo{DSSPEAKER_GEOMETRY(speakerConfig)};
    const DWORD speaker{DSSPEAKER_CONFIG(speakerConfig)};

    if(geo && (geo < DSSPEAKER_GEOMETRY_MIN || geo > DSSPEAKER_GEOMETRY_MAX))
    {
        WARN(PREFIX "Invalid speaker angle %lu\n", geo);
        return DSERR_INVALIDPARAM;
    }
    if(speaker < DSSPEAKER_HEADPHONE || speaker > DSSPEAKER_5POINT1_SURROUND)
    {
        WARN(PREFIX "Invalid speaker config %lu\n", speaker);
        return DSERR_INVALIDPARAM;
    }

    /* No-op on Vista+. */
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Initialize "
HRESULT STDMETHODCALLTYPE DSound8OAL::Initialize(const GUID *deviceId) noexcept
{
    DEBUG(PREFIX "(%p)->(%s)\n", voidp{this}, DevidPrinter{deviceId}.c_str());

    if(mShared)
    {
        WARN(PREFIX "Device already initialized\n");
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
    DEBUG(PREFIX "(%p)->(%p)\n", voidp{this}, voidp{certified});

    if(!certified)
        return DSERR_INVALIDPARAM;
    *certified = 0;

    if(!mShared)
    {
        WARN(PREFIX "Device not initialized\n");
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

    /* If the buffer wasn't in any of the groups, it's free-standing. */
    delete buffer;
}
#undef CLASS_PREFIX

#define CLASS_PREFIX "DSound8OAL::Unknown::"
HRESULT STDMETHODCALLTYPE DSound8OAL::Unknown::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE DSound8OAL::Unknown::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mUnkRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(CLASS_PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSound8OAL::Unknown::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mUnkRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(CLASS_PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        delete self;
    return ret;
}
#undef CLASS_PREFIX
