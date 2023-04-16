#include "dsoundoal.h"

#include <algorithm>
#include <optional>

#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include "buffer.h"
#include "comhelpers.h"
#include "comptr.h"
#include "dsoal.h"
#include "expected.h"
#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

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


std::optional<DWORD> GetSpeakerConfig(IMMDevice *device, const GUID &devid)
{
    ComPtr<IPropertyStore> ps;
    HRESULT hr{device->OpenPropertyStore(STGM_READ, ds::out_ptr(ps))};
    if(FAILED(hr))
    {
        WARN("GetSpeakerConfig IMMDevice::OpenPropertyStore failed: %08lx\n", hr);
        return std::nullopt;
    }

    PropVariant pv;
    hr = ps->GetValue(PKEY_AudioEndpoint_GUID, pv.get());
    if(FAILED(hr) || pv->vt != VT_LPWSTR)
    {
        WARN("GetSpeakerConfig IPropertyStore::GetValue(GUID) failed: %08lx\n", hr);
        return std::nullopt;
    }

    GUID thisId{};
    CLSIDFromString(pv->pwszVal, &thisId);

    pv.clear();

    if(devid != thisId)
        return std::nullopt;

    DWORD speakerconf{DSSPEAKER_7POINT1_SURROUND};

    hr = ps->GetValue(PKEY_AudioEndpoint_PhysicalSpeakers, pv.get());
    if(FAILED(hr))
    {
        WARN("GetSpeakerConfig IPropertyStore::GetValue(PhysicalSpeakers) failed: %08lx\n", hr);
        return speakerconf;
    }
    if(pv->vt != VT_UI4)
    {
        WARN("GetSpeakerConfig PhysicalSpeakers is not a ULONG: 0x%04x\n", pv->vt);
        return speakerconf;
    }

    const ULONG phys_speakers{pv->ulVal};

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
        else if(pv->vt != VT_UI4)
            WARN("GetSpeakerConfig FormFactor is not a ULONG: 0x%04x\n", pv->vt);
        else if(pv->ulVal == Headphones || pv->ulVal == Headset)
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
    ComPtr<IMMDeviceEnumerator> devenum;
    HRESULT hr{CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator, ds::out_ptr(devenum))};
    if(FAILED(hr))
    {
        ERR("CreateDeviceShare CoCreateInstance failed: %08lx\n", hr);
        return ds::unexpected(hr);
    }

    ComPtr<IMMDeviceCollection> coll;
    hr = devenum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, ds::out_ptr(coll));
    if(FAILED(hr))
    {
        WARN("CreateDeviceShare IMMDeviceEnumerator::EnumAudioEndpoints failed: %08lx\n", hr);
        return ds::unexpected(hr);
    }

    UINT count{};
    hr = coll->GetCount(&count);
    if(FAILED(hr))
    {
        WARN("CreateDeviceShare IMMDeviceCollection::GetCount failed: %08lx\n", hr);
        return ds::unexpected(hr);
    }

    for(UINT i{0};i < count;++i)
    {
        ComPtr<IMMDevice> device;
        hr = coll->Item(i, ds::out_ptr(device));
        if(FAILED(hr)) continue;

        if(auto config = GetSpeakerConfig(device.get(), guid))
        {
            speakerconf = *config;
            break;
        }
    }

    char drv_name[64]{};

    LPOLESTR guid_str{};
    hr = StringFromCLSID(guid, &guid_str);
    if(FAILED(hr))
    {
        ERR("CreateDeviceShare Failed to convert GUID to string\n");
        return ds::unexpected(hr);
    }
    WideCharToMultiByte(CP_UTF8, 0, guid_str, -1, drv_name, sizeof(drv_name), NULL, NULL);
    drv_name[sizeof(drv_name)-1] = 0;
    CoTaskMemFree(guid_str);
    guid_str = nullptr;

    hr = DSERR_NODRIVER;
    ALCdevicePtr aldev{alcOpenDevice(drv_name)};
    if(!aldev)
    {
        WARN("CreateDeviceShare Couldn't open device \"%s\", 0x%04x\n", drv_name,
            alcGetError(nullptr));
        return ds::unexpected(hr);
    }
    TRACE("CreateDeviceShare Opened AL device: %s\n",
        alcIsExtensionPresent(aldev.get(), "ALC_ENUMERATE_ALL_EXT") ?
        alcGetString(aldev.get(), ALC_ALL_DEVICES_SPECIFIER) :
        alcGetString(aldev.get(), ALC_DEVICE_SPECIFIER));

    const ALint attrs[]{
        ALC_MONO_SOURCES, MaxSources,
        ALC_STEREO_SOURCES, 0,
        0
    };
    ALCcontextPtr alctx{alcCreateContext(aldev.get(), attrs)};
    if(!alctx)
    {
        WARN("CreateDeviceShare Couldn't create context, 0x%04x\n", alcGetError(aldev.get()));
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
    shared->mDevice.reset(aldev.release());
    shared->mContext.reset(alctx.release());

    return shared;
}

} // namespace

#define PREFIX "SharedDevice::"
std::mutex SharedDevice::sDeviceListMutex;
std::vector<std::unique_ptr<SharedDevice>> SharedDevice::sDeviceList;

auto SharedDevice::GetById(const GUID &deviceId) noexcept
    -> ds::expected<ComPtr<SharedDevice>,HRESULT>
{
    auto find_id = [&deviceId](std::unique_ptr<SharedDevice> &device)
    { return deviceId == device->mId; };

    std::unique_lock listlock{sDeviceListMutex};
    auto sharediter = std::find_if(sDeviceList.begin(), sDeviceList.end(), find_id);
    if(sharediter != sDeviceList.end())
    {
        (*sharediter)->AddRef();
        return ComPtr<SharedDevice>{(*sharediter).get()};
    }

    auto shared = CreateDeviceShare(deviceId);
    if(!shared) return ds::unexpected(shared.error());

    sDeviceList.emplace_back(std::move(shared).value());
    return ComPtr<SharedDevice>{sDeviceList.back().get()};
}

SharedDevice::~SharedDevice()
{
    if(mContext)
    {
        if(mContext.get() == alcGetThreadContext())
            alcSetThreadContext(nullptr);
        alcDestroyContext(mContext.release());
    }
    if(mDevice)
        alcCloseDevice(mDevice.release());
}

void SharedDevice::dispose() noexcept
{
    std::lock_guard listlock{sDeviceListMutex};

    auto find_shared = [this](std::unique_ptr<SharedDevice> &shared)
    { return this == shared.get(); };

    auto shared_iter = std::find_if(sDeviceList.begin(), sDeviceList.end(), find_shared);
    if(shared_iter != sDeviceList.end())
    {
        TRACE(PREFIX "dispose Freeing shared device %s\n",
            GuidPrinter{(*shared_iter)->mId}.c_str());
        sDeviceList.erase(shared_iter);
    }
}
#undef PREFIX

BufferSubList::~BufferSubList()
{
    if(!mBuffers)
        return;

    uint64_t usemask{~mFreeMask};
    while(usemask)
    {
        int idx{ds::countr_zero(usemask)};
        usemask &= ~(1_u64 << idx);

        std::destroy_at(mBuffers + idx);
    }
    std::free(mBuffers);
}


#define PREFIX "DSound8OAL::"
ComPtr<DSound8OAL> DSound8OAL::Create(bool is8)
{
    return ComPtr<DSound8OAL>{new DSound8OAL{is8}};
}

DSound8OAL::DSound8OAL(bool is8) : mPrimaryBuffer{*this}, mIs8{is8}
{
}

DSound8OAL::~DSound8OAL() = default;


HRESULT STDMETHODCALLTYPE DSound8OAL::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG(PREFIX "QueryInterface (%p)->(%s, %p)\n", voidp{this}, GuidPrinter{riid}.c_str(),
        voidp{ppvObject});

    *ppvObject = NULL;
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
            WARN(PREFIX "QueryInterface Requesting IDirectSound8 iface for non-DS8 object\n");
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

    FIXME(PREFIX "QueryInterface Unhandled GUID: %s\n", GuidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE DSound8OAL::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSound8OAL::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        delete this;
    return ret;
}


HRESULT STDMETHODCALLTYPE DSound8OAL::CreateSoundBuffer(const DSBUFFERDESC *bufferDesc,
    IDirectSoundBuffer **dsBuffer, IUnknown *outer) noexcept
{
    DEBUG(PREFIX "CreateSoundBuffer (%p)->(%p, %p, %p)\n", voidp{this}, cvoidp{bufferDesc},
        voidp{dsBuffer}, voidp{outer});

    if(!dsBuffer)
    {
        WARN(PREFIX "CreateSoundBuffer dsBuffer is null\n");
        return DSERR_INVALIDPARAM;
    }
    *dsBuffer = nullptr;

    if(outer)
    {
        WARN(PREFIX "CreateSoundBuffer Aggregation isn't supported\n");
        return DSERR_NOAGGREGATION;
    }
    if(!bufferDesc || bufferDesc->dwSize < sizeof(DSBUFFERDESC1))
    {
        WARN(PREFIX "CreateSoundBuffer Invalid DSBUFFERDESC (%p, %lu)\n", cvoidp{bufferDesc},
            bufferDesc ? bufferDesc->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    if(!mShared)
    {
        WARN(PREFIX "CreateSoundBuffer Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    DSBUFFERDESC bufdesc{};
    std::memcpy(&bufdesc, bufferDesc, std::min<DWORD>(sizeof(bufdesc), bufferDesc->dwSize));
    bufdesc.dwSize = std::min<DWORD>(sizeof(bufdesc), bufferDesc->dwSize);

    TRACE(PREFIX "CreateSoundBuffer Requested buffer:\n"
          "    Size        = %lu\n"
          "    Flags       = 0x%08lx\n"
          "    BufferBytes = %lu\n",
        bufdesc.dwSize, bufdesc.dwFlags, bufdesc.dwBufferBytes);

    if(bufdesc.dwSize >= sizeof(DSBUFFERDESC))
    {
        if(!(bufdesc.dwFlags&DSBCAPS_CTRL3D))
        {
            if(bufdesc.guid3DAlgorithm != GUID_NULL)
            {
                /* Not fatal. Some apps pass unknown values here. */
                WARN(PREFIX "CreateSoundBuffer Unknown 3D algorithm GUID specified for non-3D buffer: %s\n",
                    GuidPrinter{bufdesc.guid3DAlgorithm}.c_str());
            }
        }
        else
        {
            TRACE("DSound8OAL::CreateSoundBuffer Requested 3D algorithm GUID: %s\n",
                GuidPrinter{bufdesc.guid3DAlgorithm}.c_str());
        }
    }

    /* OpenAL doesn't support playing with 3d and panning at same time. */
    if((bufdesc.dwFlags&(DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN)) == (DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN))
    {
        if(!mIs8)
        {
            static int once{0};
            if(!once)
            {
                ++once;
                FIXME(PREFIX "CreateSoundBuffer Buffers with 3D and pan control ignore panning\n");
            }
        }
        else
        {
            WARN(PREFIX "CreateSoundBuffer Cannot create buffers with 3D and pan control\n");
            return DSERR_INVALIDPARAM;
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
            group.mBuffers = reinterpret_cast<Buffer*>(std::malloc(sizeof(Buffer[64])));
            if(group.mBuffers)
            {
                mSecondaryBuffers.emplace_back(std::move(group));
                sublist = &mSecondaryBuffers.back();
            }
        }
        if(!sublist)
            return DSERR_OUTOFMEMORY;

        int idx{ds::countr_zero(sublist->mFreeMask)};
        ComPtr<Buffer> buffer{::new(sublist->mBuffers + idx) Buffer{*this, mIs8}};
        sublist->mFreeMask &= ~(1_u64 << idx);
        lock.unlock();

        hr = buffer->Initialize(as<IDirectSound*>(), &bufdesc);
        if(SUCCEEDED(hr))
            *dsBuffer = buffer.release()->as<IDirectSoundBuffer*>();
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::GetCaps(DSCAPS *dsCaps) noexcept
{
    DEBUG(PREFIX "GetCaps (%p)->(%p)\n", voidp{this}, voidp{dsCaps});

    if(!mShared)
    {
        WARN(PREFIX "GetCaps Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(!dsCaps || dsCaps->dwSize < sizeof(*dsCaps))
    {
        WARN(PREFIX "GetCaps Invalid DSCAPS (%p, %lu)\n", voidp{dsCaps},
            dsCaps ? dsCaps->dwSize : 0lu);
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

HRESULT STDMETHODCALLTYPE DSound8OAL::DuplicateSoundBuffer(IDirectSoundBuffer *origBuffer,
    IDirectSoundBuffer **dupBuffer) noexcept
{
    FIXME(PREFIX "DuplicateSoundBuffer (%p)->(%p, %p)\n", voidp{this}, voidp{origBuffer},
        voidp{dupBuffer});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::SetCooperativeLevel(HWND hwnd, DWORD level) noexcept
{
    DEBUG(PREFIX "SetCooperativeLevel (%p)->(%p, %lu)\n", voidp{this}, voidp{hwnd}, level);

    if(!mShared)
    {
        WARN(PREFIX "SetCooperativeLevel Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(level > DSSCL_WRITEPRIMARY || level < DSSCL_NORMAL)
    {
        WARN(PREFIX "SetCooperativeLevel Invalid cooperative level: %lu\n", level);
        return DSERR_INVALIDPARAM;
    }

    if(level == DSSCL_WRITEPRIMARY)
    {
        FIXME(PREFIX "SetCooperativeLevel DSSCL_WRITEPRIMARY not currently supported\n");
        return DSERR_INVALIDPARAM;
    }

    mPrioLevel = level;

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::Compact() noexcept
{
    DEBUG(PREFIX "Compact (%p)->()\n", voidp{this});

    if(!mShared)
    {
        WARN(PREFIX "Compact Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(mPrioLevel < DSSCL_PRIORITY)
    {
        WARN("DSound8OAL::Compact Cooperative level too low: %lu\n", mPrioLevel);
        return DSERR_PRIOLEVELNEEDED;
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::GetSpeakerConfig(DWORD *speakerConfig) noexcept
{
    DEBUG(PREFIX "GetSpeakerConfig (%p)->(%p)\n", voidp{this}, voidp{speakerConfig});

    if(!speakerConfig)
        return DSERR_INVALIDPARAM;
    *speakerConfig = 0;

    if(!mShared)
    {
        WARN(PREFIX "GetSpeakerConfig Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    *speakerConfig = mShared->mSpeakerConfig;

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::SetSpeakerConfig(DWORD speakerConfig) noexcept
{
    DEBUG(PREFIX "SetSpeakerConfig (%p)->(%lx)\n", voidp{this}, speakerConfig);

    if(!mShared)
    {
        WARN(PREFIX "SetSpeakerConfig Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    const DWORD geo{DSSPEAKER_GEOMETRY(speakerConfig)};
    const DWORD speaker{DSSPEAKER_CONFIG(speakerConfig)};

    if(geo && (geo < DSSPEAKER_GEOMETRY_MIN || geo > DSSPEAKER_GEOMETRY_MAX))
    {
        WARN(PREFIX "SetSpeakerConfig Invalid speaker angle %lu\n", geo);
        return DSERR_INVALIDPARAM;
    }
    if(speaker < DSSPEAKER_HEADPHONE || speaker > DSSPEAKER_5POINT1_SURROUND)
    {
        WARN(PREFIX "SetSpeakerConfig Invalid speaker config %lu\n", speaker);
        return DSERR_INVALIDPARAM;
    }

    /* No-op on Vista+. */
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::Initialize(const GUID *deviceId) noexcept
{
    DEBUG(PREFIX "Initialize (%p)->(%s)\n", voidp{this}, GuidPrinter{deviceId}.c_str());

    if(mShared)
    {
        WARN(PREFIX "Initialize Device already initialized\n");
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

    mPrimaryBuffer.setContext(mShared->mContext.get());

    /* Preallocate some groups for the number of "hardware" buffers we can do.
     * This will grow as needed.
     */
    const size_t numGroups{(mShared->mMaxHwSources+63) / 64};
    for(size_t i{0};i < numGroups;++i)
    {
        BufferSubList group;
        group.mBuffers = reinterpret_cast<Buffer*>(std::malloc(sizeof(Buffer[64])));
        if(!group.mBuffers) break;

        mSecondaryBuffers.emplace_back(std::move(group));
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::VerifyCertification(DWORD *certified) noexcept
{
    DEBUG(PREFIX "VerifyCertification (%p)->(%p)\n", voidp{this}, voidp{certified});

    if(!certified)
        return DSERR_INVALIDPARAM;
    *certified = 0;

    if(!mShared)
    {
        WARN(PREFIX "VerifyCertification Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    *certified = DS_CERTIFIED;

    return DS_OK;
}

void DSound8OAL::dispose(Buffer *buffer) noexcept
{
    std::lock_guard lock{mDsMutex};
    /* Find the group the given buffer belongs in, then destruct it and mark it
     * as free.
     */
    for(auto &group : mSecondaryBuffers)
    {
        ptrdiff_t idx{buffer - group.mBuffers};
        if(static_cast<std::make_unsigned_t<ptrdiff_t>>(idx) < 64)
        {
            std::destroy_at(group.mBuffers + idx);
            group.mFreeMask |= 1_u64 << idx;
            return;
        }
    }

    /* If the buffer wasn't in any of the groups, it's free-standing. */
    delete buffer;
}
#undef PREFIX

#define PREFIX "DSound8OAL::UnknownImpl::"
HRESULT STDMETHODCALLTYPE DSound8OAL::UnknownImpl::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE DSound8OAL::UnknownImpl::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mUnkRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSound8OAL::UnknownImpl::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mUnkRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        delete self;
    return ret;
}
#undef PREFIX
