#include "primarybuffer.h"

#include <vfwmsgs.h>

#include "buffer.h"
#include "dsoal.h"
#include "dsoundoal.h"
#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

/* The primary buffer has a fixed size, apprently. */
constexpr size_t PrimaryBufSize{32768};

} // namespace

#define CLASS_PREFIX "Primary::"
PrimaryBuffer::PrimaryBuffer(DSound8OAL &parent) : mParent{parent}, mMutex{parent.getMutex()}
{
    /* Make sure the format is valid, store 16-bit stereo 44.1khz by default. */
    mFormat.Format.wFormatTag = WAVE_FORMAT_PCM;
    mFormat.Format.nChannels = 2;
    mFormat.Format.nSamplesPerSec = 44100;
    mFormat.Format.nAvgBytesPerSec = 44100 * 4;
    mFormat.Format.nBlockAlign = 4;
    mFormat.Format.wBitsPerSample = 16;
    mFormat.Format.cbSize = 0;
}

PrimaryBuffer::~PrimaryBuffer() = default;

auto PrimaryBuffer::createWriteEmu(DWORD flags) noexcept -> HRESULT
{
    auto emudesc = DSBUFFERDESC{};
    emudesc.dwSize = sizeof(emudesc);
    emudesc.dwFlags = DSBCAPS_LOCHARDWARE | (flags&DSBCAPS_CTRLPAN);
    emudesc.dwBufferBytes = PrimaryBufSize - (PrimaryBufSize%mFormat.Format.nBlockAlign);
    emudesc.lpwfxFormat = &mFormat.Format;

    auto emu = std::unique_ptr<Buffer>{new(std::nothrow) Buffer{mParent, false, nullptr}};
    if(!emu) return DSERR_OUTOFMEMORY;

    if(auto hr = emu->Initialize(mParent.as<IDirectSound*>(), &emudesc); FAILED(hr))
        return hr;

    mWriteEmu = std::move(emu);
    return DS_OK;
}

void PrimaryBuffer::destroyWriteEmu() noexcept
{ mWriteEmu = nullptr; }


#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    if(!ppvObject)
        return E_POINTER;
    *ppvObject = nullptr;

    if(riid == IID_IUnknown)
    {
        AddRef();
        *ppvObject = as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundBuffer)
    {
        AddRef();
        *ppvObject = as<IDirectSoundBuffer*>();
        return S_OK;
    }
    if(riid == IID_IDirectSound3DListener)
    {
        mListener3D.AddRef();
        *ppvObject = mListener3D.as<IDirectSound3DListener*>();
        return S_OK;
    }
    if(riid == IID_IKsPropertySet)
    {
        mProp.AddRef();
        *ppvObject = mProp.as<IKsPropertySet*>();
        return S_OK;
    }

    FIXME("Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE PrimaryBuffer::AddRef() noexcept
{
    const auto prev = mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);

    /* Clear the flags when getting the first reference, so it can be
     * reinitialized.
     */
    if(prev == 0)
        mFlags = 0;

    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE PrimaryBuffer::Release() noexcept
{
    /* NOTE: Some buggy apps try to release after hitting 0 references, so
     * prevent underflowing the reference counter.
     */
    ULONG ret{mDsRef.load(std::memory_order_relaxed)};
    do {
        if(ret == 0) [[unlikely]]
        {
            WARN("({}) ref already {}", voidp{this}, ret);
            return ret;
        }
    } while(!mDsRef.compare_exchange_weak(ret, ret-1, std::memory_order_relaxed));
    ret -= 1;
    DEBUG("({}) ref {}", voidp{this}, ret);

    /* The primary buffer is a static object and should not be deleted. */
    mTotalRef.fetch_sub(1u, std::memory_order_relaxed);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetCaps "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetCaps(DSBCAPS *bufferCaps) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{bufferCaps});

    if(!bufferCaps || bufferCaps->dwSize < sizeof(*bufferCaps))
    {
        WARN("Invalid DSBCAPS ({}, {})", voidp{bufferCaps}, bufferCaps ? bufferCaps->dwSize : 0lu);
        return DSERR_INVALIDPARAM;
    }

    bufferCaps->dwFlags = mFlags;
    bufferCaps->dwBufferBytes = PrimaryBufSize;
    bufferCaps->dwUnlockTransferRate = 0;
    bufferCaps->dwPlayCpuOverhead = 0;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetCurrentPosition "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetCurrentPosition(DWORD *playCursor, DWORD *writeCursor) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, voidp{playCursor}, voidp{writeCursor});

    std::lock_guard lock{mMutex};
    if(mWriteEmu)
        return mWriteEmu->GetCurrentPosition(playCursor, writeCursor);
    return DSERR_PRIOLEVELNEEDED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetFormat "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetFormat(WAVEFORMATEX *wfx, DWORD sizeAllocated, DWORD *sizeWritten) noexcept
{
    DEBUG("({})->({}, {}, {})", voidp{this}, voidp{wfx}, sizeAllocated, voidp{sizeWritten});

    if(!wfx && !sizeWritten)
    {
        WARN("Cannot report format or format size");
        return DSERR_INVALIDPARAM;
    }

    std::lock_guard lock{mMutex};
    const DWORD size{sizeof(mFormat.Format) + mFormat.Format.cbSize};
    if(sizeWritten)
        *sizeWritten = size;
    if(wfx)
    {
        if(sizeAllocated < size)
            return DSERR_INVALIDPARAM;
        std::memcpy(wfx, &mFormat.Format, size);
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetVolume "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetVolume(LONG *volume) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{volume});

    if(!volume)
        return DSERR_INVALIDPARAM;

    if(!(mFlags&DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;

    std::lock_guard lock{mMutex};
    *volume = mVolume;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetPan "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetPan(LONG *pan) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{pan});

    if(!pan)
        return DSERR_INVALIDPARAM;

    if(!(mFlags&DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;

    std::lock_guard lock{mMutex};
    *pan = mPan;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetFrequency "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetFrequency(DWORD *frequency) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{frequency});

    if(!frequency)
        return DSERR_INVALIDPARAM;

    if(!(mFlags&DSBCAPS_CTRLFREQUENCY))
        return DSERR_CONTROLUNAVAIL;

    std::lock_guard lock{mMutex};
    *frequency = mFormat.Format.nSamplesPerSec;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetStatus "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetStatus(DWORD *status) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{status});

    if(!status)
        return DSERR_INVALIDPARAM;

    std::lock_guard lock{mMutex};
    bool playing{mPlaying};
    if(!playing && mParent.getPriorityLevel() < DSSCL_WRITEPRIMARY)
    {
        for(auto &group : mParent.getSecondaryBuffers())
        {
            uint64_t usemask{~group.mFreeMask};
            while(usemask)
            {
                auto idx = static_cast<unsigned int>(ds::countr_zero(usemask));
                usemask &= ~(1_u64 << idx);
                Buffer &buffer = (*group.mBuffers)[idx];

                if(const ALuint source{buffer.getSource()})
                {
                    ALint state{};
                    alGetSourceiDirect(mContext, source, AL_SOURCE_STATE, &state);
                    playing = (state == AL_PLAYING);
                    if(playing) break;
                }
            }
            if(playing)
                break;
        }
    }

    if(playing)
    {
        *status = DSBSTATUS_PLAYING|DSBSTATUS_LOOPING;
        if((mFlags&DSBCAPS_LOCDEFER))
            *status |= DSBSTATUS_LOCHARDWARE;
    }
    else
        *status = 0;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Initialize "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Initialize(IDirectSound *directSound, const DSBUFFERDESC *dsBufferDesc) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, voidp{directSound}, cvoidp{dsBufferDesc});

    if(!dsBufferDesc || dsBufferDesc->lpwfxFormat || dsBufferDesc->dwBufferBytes)
    {
        WARN("Bad DSBUFFERDESC");
        return DSERR_INVALIDPARAM;
    }

    static constexpr DWORD BadFlags{DSBCAPS_CTRLFX | DSBCAPS_CTRLPOSITIONNOTIFY
        | DSBCAPS_LOCSOFTWARE};
    if((dsBufferDesc->dwFlags&BadFlags))
    {
        WARN("Bad dwFlags {:08x}", dsBufferDesc->dwFlags);
        return DSERR_INVALIDPARAM;
    }

    std::lock_guard lock{mMutex};
    if(mFlags != 0)
        return DSERR_ALREADYINITIALIZED;

    if(mParent.getPriorityLevel() == DSSCL_WRITEPRIMARY)
    {
        mWriteEmu = nullptr;
        if(auto hr = createWriteEmu(dsBufferDesc->dwFlags); FAILED(hr))
            return hr;
    }

    mFlags = dsBufferDesc->dwFlags | DSBCAPS_LOCHARDWARE;

    mImmediate.dwSize = sizeof(mImmediate);
    mImmediate.vPosition.x = 0.0f;
    mImmediate.vPosition.y = 0.0f;
    mImmediate.vPosition.z = 0.0f;
    mImmediate.vVelocity.x = 0.0f;
    mImmediate.vVelocity.y = 0.0f;
    mImmediate.vVelocity.z = 0.0f;
    mImmediate.vOrientFront.x = 0.0f;
    mImmediate.vOrientFront.y = 0.0f;
    mImmediate.vOrientFront.z = 1.0f;
    mImmediate.vOrientTop.x = 0.0f;
    mImmediate.vOrientTop.y = 1.0f;
    mImmediate.vOrientTop.z = 0.0f;
    mImmediate.flDistanceFactor = DS3D_DEFAULTDISTANCEFACTOR;
    mImmediate.flRolloffFactor = DS3D_DEFAULTROLLOFFFACTOR;
    mImmediate.flDopplerFactor = DS3D_DEFAULTDOPPLERFACTOR;
    mDeferred = mImmediate;
    mDirty.reset();

    setParams(mDeferred, ~0llu);

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Lock "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Lock(DWORD offset, DWORD bytes, void **audioPtr1, DWORD *audioBytes1, void **audioPtr2, DWORD *audioBytes2, DWORD flags) noexcept
{
    DEBUG("({})->({}, {}, {}, {}, {}, {}, {})", voidp{this}, offset, bytes, voidp{audioPtr1},
        voidp{audioBytes1}, voidp{audioPtr2}, voidp{audioBytes2}, flags);

    std::lock_guard lock{mMutex};
    if(mWriteEmu)
        return mWriteEmu->Lock(offset, bytes, audioPtr1, audioBytes1, audioPtr2, audioBytes2, flags);
    return DSERR_PRIOLEVELNEEDED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Play "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Play(DWORD reserved1, DWORD reserved2, DWORD flags) noexcept
{
    DEBUG("({})->({}, {}, {})", voidp{this}, reserved1, reserved2, flags);

    if(!(flags & DSBPLAY_LOOPING))
    {
        WARN("Flags ({:08x}) not set to DSBPLAY_LOOPING", flags);
        return DSERR_INVALIDPARAM;
    }

    std::lock_guard lock{mMutex};
    auto hr = S_OK;
    if(mWriteEmu)
        hr = mWriteEmu->Play(reserved1, reserved2, flags);

    if(SUCCEEDED(hr))
        mPlaying = true;

    return hr;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetCurrentPosition "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetCurrentPosition(DWORD newPosition) noexcept
{
    FIXME("({})->({})", voidp{this}, newPosition);
    return DSERR_INVALIDCALL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetFormat "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetFormat(const WAVEFORMATEX *wfx) noexcept
{
    DEBUG("({})->({})", voidp{this}, cvoidp{wfx});

    if(!wfx)
    {
        WARN("Missing format");
        return DSERR_INVALIDPARAM;
    }

    std::lock_guard lock{mMutex};
    if(mParent.getPriorityLevel() < DSSCL_PRIORITY)
        return DSERR_PRIOLEVELNEEDED;

    static constexpr WORD ExtExtraSize{sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)};
    if(wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        /* Fail silently.. */
        if(wfx->cbSize < ExtExtraSize)
        {
            WARN("EXTENSIBLE size too small ({}, expected {}). Ignoring...", wfx->cbSize,
                ExtExtraSize);
            return DS_OK;
        }

        /* NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) */
        auto *wfe = CONTAINING_RECORD(wfx, const WAVEFORMATEXTENSIBLE, Format);
        TRACE("Requested primary format:\n"
            "    FormatTag          = 0x{:04x}\n"
            "    Channels           = {}\n"
            "    SamplesPerSec      = {}\n"
            "    AvgBytesPerSec     = {}\n"
            "    BlockAlign         = {}\n"
            "    BitsPerSample      = {}\n"
            "    ValidBitsPerSample = {}\n"
            "    ChannelMask        = 0x{:08x}\n"
            "    SubFormat          = {}",
            wfe->Format.wFormatTag, wfe->Format.nChannels, wfe->Format.nSamplesPerSec,
            wfe->Format.nAvgBytesPerSec, wfe->Format.nBlockAlign, wfe->Format.wBitsPerSample,
            wfe->Samples.wValidBitsPerSample, wfe->dwChannelMask,
            FmtidPrinter{wfe->SubFormat}.c_str());
        /* NOLINTEND(cppcoreguidelines-pro-type-union-access) */
    }
    else
    {
        TRACE("Requested primary format:\n"
            "    FormatTag      = 0x{:04x}\n"
            "    Channels       = {}\n"
            "    SamplesPerSec  = {}\n"
            "    AvgBytesPerSec = {}\n"
            "    BlockAlign     = {}\n"
            "    BitsPerSample  = {}",
            wfx->wFormatTag, wfx->nChannels, wfx->nSamplesPerSec, wfx->nAvgBytesPerSec,
            wfx->nBlockAlign, wfx->wBitsPerSample);
    }

    auto copy_format = [wfx](WAVEFORMATEXTENSIBLE &dst) -> HRESULT
    {
        if(wfx->nChannels <= 0)
        {
            WARN("Invalid Channels {}", wfx->nChannels);
            return DSERR_INVALIDPARAM;
        }
        if(wfx->nSamplesPerSec < DSBFREQUENCY_MIN || wfx->nSamplesPerSec > DSBFREQUENCY_MAX)
        {
            WARN("Invalid SamplesPerSec {}", wfx->nSamplesPerSec);
            return DSERR_INVALIDPARAM;
        }
        if(wfx->nBlockAlign <= 0)
        {
            WARN("Invalid BlockAlign {}", wfx->nBlockAlign);
            return DSERR_INVALIDPARAM;
        }
        if(wfx->wBitsPerSample == 0 || (wfx->wBitsPerSample%8) != 0)
        {
            WARN("Invalid BitsPerSample {}", wfx->wBitsPerSample);
            return DSERR_INVALIDPARAM;
        }
        if(wfx->nBlockAlign != wfx->nChannels*wfx->wBitsPerSample/8)
        {
            WARN("Invalid BlockAlign {} (expected {} = {}*{}/8)", wfx->nBlockAlign,
                wfx->nChannels*wfx->wBitsPerSample/8, wfx->nChannels, wfx->wBitsPerSample);
            return DSERR_INVALIDPARAM;
        }
        if(wfx->nAvgBytesPerSec != wfx->nBlockAlign*wfx->nSamplesPerSec)
        {
            WARN("Invalid AvgBytesPerSec {} (expected {} = {}*{})", wfx->nAvgBytesPerSec,
                wfx->nSamplesPerSec*wfx->nBlockAlign, wfx->nSamplesPerSec, wfx->nBlockAlign);
            return DSERR_INVALIDPARAM;
        }

        if(wfx->wFormatTag == WAVE_FORMAT_PCM)
        {
            if(wfx->wBitsPerSample > 32)
                return DSERR_INVALIDPARAM;
            dst = {};
        }
        else if(wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        {
            if(wfx->wBitsPerSample != 32)
                return DSERR_INVALIDPARAM;
            dst = {};
        }
        else if(wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            auto *fromx = CONTAINING_RECORD(wfx, const WAVEFORMATEXTENSIBLE, Format);

            /* NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) */
            if(fromx->Samples.wValidBitsPerSample > fromx->Format.wBitsPerSample)
                return DSERR_INVALIDPARAM;

            if(fromx->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
            {
                if(wfx->wBitsPerSample > 32)
                    return DSERR_INVALIDPARAM;
            }
            else if(fromx->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            {
                if(wfx->wBitsPerSample != 32)
                    return DSERR_INVALIDPARAM;
            }
            else
            {
                FIXME("Unhandled extensible format: {}", GuidPrinter{fromx->SubFormat}.c_str());
                return DSERR_INVALIDPARAM;
            }

            dst = {};
            dst.Format.cbSize = ExtExtraSize;
            dst.Samples.wValidBitsPerSample = fromx->Samples.wValidBitsPerSample;
            if(!dst.Samples.wValidBitsPerSample)
                dst.Samples.wValidBitsPerSample = fromx->Format.wBitsPerSample;
            dst.dwChannelMask = fromx->dwChannelMask;
            dst.SubFormat = fromx->SubFormat;
            /* NOLINTEND(cppcoreguidelines-pro-type-union-access) */
        }
        else
        {
            FIXME("Unhandled format tag {:04x}", wfx->wFormatTag);
            return DSERR_INVALIDPARAM;
        }

        dst.Format.wFormatTag = wfx->wFormatTag;
        dst.Format.nChannels = wfx->nChannels;
        dst.Format.nSamplesPerSec = wfx->nSamplesPerSec;
        dst.Format.nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
        dst.Format.nBlockAlign = static_cast<WORD>(wfx->wBitsPerSample * wfx->nChannels / 8);
        dst.Format.wBitsPerSample = wfx->wBitsPerSample;
        return DS_OK;
    };

    auto hr = copy_format(mFormat);
    if(SUCCEEDED(hr) && mWriteEmu)
    {
        mWriteEmu = nullptr;
        hr = createWriteEmu(mFlags);
    }
    return hr;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetVolume "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetVolume(LONG volume) noexcept
{
    FIXME("({})->({})", voidp{this}, volume);

    if(volume > DSBVOLUME_MAX || volume < DSBVOLUME_MIN)
    {
        WARN("Invalid volume ({})", volume);
        return DSERR_INVALIDPARAM;
    }

    std::lock_guard lock{mMutex};
    if(!(mFlags&DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;

    mVolume = volume;
    alListenerfDirect(mContext, AL_GAIN, mB_to_gain(volume));

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetPan "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetPan(LONG pan) noexcept
{
    FIXME("({})->({}): stub", voidp{this}, pan);

    if(pan < DSBPAN_LEFT || pan > DSBPAN_RIGHT)
    {
        WARN("Invalid pan ({})", pan);
        return DSERR_INVALIDPARAM;
    }

    std::lock_guard lock{mMutex};
    if(!(mFlags&DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;

    auto hr = S_OK;
    if(mWriteEmu)
        hr = mWriteEmu->SetPan(pan);
    if(SUCCEEDED(hr))
        mPan = pan;

    return hr;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetFrequency "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetFrequency(DWORD frequency) noexcept
{
    FIXME("({})->({})", voidp{this}, frequency);
    return DSERR_CONTROLUNAVAIL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Stop "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Stop() noexcept
{
    DEBUG("({})->()", voidp{this});

    std::lock_guard lock{mMutex};
    auto hr = S_OK;
    if(mWriteEmu)
    {
        /* Stopping the primary buffer also resets its position to 0. */
        hr = mWriteEmu->Stop();
        if(SUCCEEDED(hr))
            mWriteEmu->SetCurrentPosition(0);
    }
    if(SUCCEEDED(hr))
        mPlaying = false;

    return hr;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Unlock "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Unlock(void *audioPtr1, DWORD audioBytes1, void *audioPtr2, DWORD audioBytes2) noexcept
{
    DEBUG("({})->({}, {}, {}, {})", voidp{this}, audioPtr1, audioBytes1, audioPtr2, audioBytes2);

    std::lock_guard lock{mMutex};
    if(mWriteEmu)
        return mWriteEmu->Unlock(audioPtr1, audioBytes1, audioPtr2, audioBytes2);
    return DSERR_INVALIDCALL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Restore "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Restore() noexcept
{
    DEBUG("({})->()", voidp{this});

    std::lock_guard lock{mMutex};
    if(mWriteEmu)
        return mWriteEmu->Restore();
    return DS_OK;
}
#undef PREFIX


void PrimaryBuffer::setParams(const DS3DLISTENER &params, const std::bitset<FlagCount> flags)
{
    if(flags.test(Position))
        mImmediate.vPosition = params.vPosition;
    if(flags.test(Velocity))
        mImmediate.vVelocity = params.vVelocity;
    if(flags.test(Orientation))
    {
        mImmediate.vOrientFront = params.vOrientFront;
        mImmediate.vOrientTop = params.vOrientTop;
    }
    if(flags.test(DistanceFactor))
        mImmediate.flDistanceFactor = params.flDistanceFactor;
    if(flags.test(RolloffFactor))
        mImmediate.flRolloffFactor = params.flRolloffFactor;
    if(flags.test(DopplerFactor))
        mImmediate.flDopplerFactor = params.flDopplerFactor;

    if(flags.test(Position))
        alListener3fDirect(mContext, AL_POSITION, params.vPosition.x, params.vPosition.y,
            -params.vPosition.z);
    if(flags.test(Velocity))
        alListener3fDirect(mContext, AL_VELOCITY, params.vVelocity.x, params.vVelocity.y,
            -params.vVelocity.z);
    if(flags.test(Orientation))
    {
        const std::array<ALfloat,6> ori{{
            params.vOrientFront.x, params.vOrientFront.y, -params.vOrientFront.z,
            params.vOrientTop.x, params.vOrientTop.y, -params.vOrientTop.z}};
        alListenerfvDirect(mContext, AL_ORIENTATION, ori.data());
    }
    if(flags.test(DistanceFactor))
    {
        alSpeedOfSoundDirect(mContext, 343.3f / params.flDistanceFactor);
        if(mParent.haveExtension(EXT_EFX))
            alListenerfDirect(mContext, AL_METERS_PER_UNIT, params.flDistanceFactor);
    }
    if(flags.test(RolloffFactor))
    {
        for(Buffer *buffer : mParent.get3dBuffers())
        {
            if(buffer->getCurrentMode() != DS3DMODE_DISABLE)
            {
                if(ALuint source{buffer->getSource()})
                    alSourcefDirect(mContext, source, AL_ROLLOFF_FACTOR, params.flRolloffFactor);
            }
        }
    }
    if(flags.test(DopplerFactor))
        alDopplerFactorDirect(mContext, params.flDopplerFactor);
}

void PrimaryBuffer::commit() noexcept
{
    if(auto flags = std::exchange(mDirty, 0); flags.any())
    {
        setParams(mDeferred, flags);
        alGetErrorDirect(mContext);
    }

    for(Buffer *buffer : mParent.get3dBuffers())
        buffer->commit();
    alGetErrorDirect(mContext);
}
#undef CLASS_PREFIX

#define CLASS_PREFIX "Listener3D::"
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::QueryInterface(REFIID riid, void** ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE PrimaryBuffer::Listener3D::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mDs3dRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE PrimaryBuffer::Listener3D::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mDs3dRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetAllParameters "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::GetAllParameters(DS3DLISTENER *listener) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{listener});

    if(!listener || listener->dwSize < sizeof(*listener))
    {
        WARN("Invalid DS3DLISTENER ({} {})", voidp{listener}, listener ? listener->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    listener->vPosition = self->mImmediate.vPosition;
    listener->vVelocity = self->mImmediate.vVelocity;
    listener->vOrientFront = self->mImmediate.vOrientFront;
    listener->vOrientTop = self->mImmediate.vOrientTop;
    listener->flDistanceFactor = self->mImmediate.flDistanceFactor;
    listener->flRolloffFactor = self->mImmediate.flRolloffFactor;
    listener->flDopplerFactor = self->mImmediate.flDopplerFactor;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetDistanceFactor "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::GetDistanceFactor(D3DVALUE *distanceFactor) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{distanceFactor});

    if(!distanceFactor)
        return DSERR_INVALIDPARAM;

    auto self = impl_from_base();
    *distanceFactor = self->mImmediate.flDistanceFactor;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetDopplerFactor "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::GetDopplerFactor(D3DVALUE *dopplerFactor) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{dopplerFactor});

    if(!dopplerFactor)
        return DSERR_INVALIDPARAM;

    auto self = impl_from_base();
    *dopplerFactor = self->mImmediate.flDopplerFactor;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetOrientation "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::GetOrientation(D3DVECTOR *orientFront, D3DVECTOR *orientTop) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, voidp{orientFront}, voidp{orientTop});

    if(!orientFront || !orientTop)
        return DSERR_INVALIDPARAM;

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *orientFront = self->mImmediate.vOrientFront;
    *orientTop = self->mImmediate.vOrientTop;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetPosition "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::GetPosition(D3DVECTOR *position) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{position});

    if(!position)
        return DSERR_INVALIDPARAM;

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *position = self->mImmediate.vPosition;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetRolloffFactor "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::GetRolloffFactor(D3DVALUE *rolloffFactor) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{rolloffFactor});

    if(!rolloffFactor)
        return DSERR_INVALIDPARAM;

    auto self = impl_from_base();
    *rolloffFactor = self->mImmediate.flRolloffFactor;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetVelocity "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::GetVelocity(D3DVECTOR *velocity) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{velocity});

    if(!velocity)
        return DSERR_INVALIDPARAM;

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *velocity = self->mImmediate.vVelocity;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetAllParameters "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::SetAllParameters(const DS3DLISTENER *listener, DWORD apply) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, cvoidp{listener}, apply);

    if(!listener || listener->dwSize < sizeof(*listener))
    {
        WARN("Invalid parameter ({} {})", cvoidp{listener}, listener ? listener->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    if(listener->flDistanceFactor > DS3D_MAXDISTANCEFACTOR
        || listener->flDistanceFactor < DS3D_MINDISTANCEFACTOR)
    {
        WARN("Invalid distance factor ({:f})", listener->flDistanceFactor);
        return DSERR_INVALIDPARAM;
    }

    if(listener->flDopplerFactor > DS3D_MAXDOPPLERFACTOR
        || listener->flDopplerFactor < DS3D_MINDOPPLERFACTOR)
    {
        WARN("Invalid doppler factor ({:f})", listener->flDopplerFactor);
        return DSERR_INVALIDPARAM;
    }

    if(listener->flRolloffFactor < DS3D_MINROLLOFFFACTOR
        || listener->flRolloffFactor > DS3D_MAXROLLOFFFACTOR)
    {
        WARN("Invalid rolloff factor ({:f})", listener->flRolloffFactor);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred = *listener;
        self->mDeferred.dwSize = sizeof(self->mDeferred);
        self->mDirty.set();
    }
    else
    {
        alcSuspendContext(self->mContext);
        self->setParams(*listener, ~0ull);
        alcProcessContext(self->mContext);
    }

    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetDistanceFactor "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::SetDistanceFactor(D3DVALUE distanceFactor, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {})", voidp{this}, distanceFactor, apply);

    if(distanceFactor < DS3D_MINDISTANCEFACTOR || distanceFactor > DS3D_MAXDISTANCEFACTOR)
    {
        WARN("Invalid parameter {:f}", distanceFactor);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.flDistanceFactor = distanceFactor;
        self->mDirty.set(DistanceFactor);
    }
    else
    {
        alcSuspendContext(self->mContext);
        self->mImmediate.flDistanceFactor = distanceFactor;
        alSpeedOfSoundDirect(self->mContext, 343.3f / distanceFactor);
        if(self->mParent.haveExtension(EXT_EFX))
            alListenerfDirect(self->mContext, AL_METERS_PER_UNIT, distanceFactor);
        alGetErrorDirect(self->mContext);
        alcProcessContext(self->mContext);
    }

    return S_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetDopplerFactor "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::SetDopplerFactor(D3DVALUE dopplerFactor, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {})", voidp{this}, dopplerFactor, apply);

    if(dopplerFactor < DS3D_MINDOPPLERFACTOR || dopplerFactor > DS3D_MAXDOPPLERFACTOR)
    {
        WARN("Invalid parameter {:f}", dopplerFactor);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.flDopplerFactor = dopplerFactor;
        self->mDirty.set(DopplerFactor);
    }
    else
    {
        self->mImmediate.flDopplerFactor = dopplerFactor;
        alDopplerFactorDirect(self->mContext, dopplerFactor);
        alGetErrorDirect(self->mContext);
    }

    return S_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetOrientation "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::SetOrientation(D3DVALUE xFront, D3DVALUE yFront, D3DVALUE zFront, D3DVALUE xTop, D3DVALUE yTop, D3DVALUE zTop, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {:f}, {:f}, {:f}, {:f}, {:f}, {})", voidp{this}, xFront, yFront, zFront,
        xTop, yTop, zTop, apply);

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.vOrientFront.x = xFront;
        self->mDeferred.vOrientFront.y = yFront;
        self->mDeferred.vOrientFront.z = zFront;
        self->mDeferred.vOrientTop.x = xTop;
        self->mDeferred.vOrientTop.y = yTop;
        self->mDeferred.vOrientTop.z = zTop;
        self->mDirty.set(Orientation);
    }
    else
    {
        self->mImmediate.vOrientFront.x = xFront;
        self->mImmediate.vOrientFront.y = yFront;
        self->mImmediate.vOrientFront.z = zFront;
        self->mImmediate.vOrientTop.x = xTop;
        self->mImmediate.vOrientTop.y = yTop;
        self->mImmediate.vOrientTop.z = zTop;

        const std::array<ALfloat,6> ori{{xFront, yFront, -zFront, xTop, yTop, -zTop}};
        alListenerfvDirect(self->mContext, AL_ORIENTATION, ori.data());
        alGetErrorDirect(self->mContext);
    }

    return S_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetPosition "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {:f}, {:f}, {})", voidp{this}, x, y, z, apply);

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.vPosition.x = x;
        self->mDeferred.vPosition.y = y;
        self->mDeferred.vPosition.z = z;
        self->mDirty.set(Position);
    }
    else
    {
        self->mImmediate.vPosition.x = x;
        self->mImmediate.vPosition.y = y;
        self->mImmediate.vPosition.z = z;

        alListener3fDirect(self->mContext, AL_POSITION, x, y, -z);
        alGetErrorDirect(self->mContext);
    }

    return S_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetRolloffFactor "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::SetRolloffFactor(D3DVALUE rolloffFactor, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {})", voidp{this}, rolloffFactor, apply);

    if(rolloffFactor < DS3D_MINROLLOFFFACTOR || rolloffFactor > DS3D_MAXROLLOFFFACTOR)
    {
        WARN("Invalid parameter {:f}", rolloffFactor);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.flRolloffFactor = rolloffFactor;
        self->mDirty.set(RolloffFactor);
    }
    else
    {
        alcSuspendContext(self->mContext);
        self->mImmediate.flRolloffFactor = rolloffFactor;

        for(Buffer *buffer : self->mParent.get3dBuffers())
        {
            if(buffer->getCurrentMode() != DS3DMODE_DISABLE)
            {
                if(ALuint source{buffer->getSource()})
                    alSourcefDirect(self->mContext, source, AL_ROLLOFF_FACTOR, rolloffFactor);
            }
        }
        alGetErrorDirect(self->mContext);
        alcProcessContext(self->mContext);
    }

    return S_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetVelocity "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {:f}, {:f}, {})", voidp{this}, x, y, z, apply);

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.vVelocity.x = x;
        self->mDeferred.vVelocity.y = y;
        self->mDeferred.vVelocity.z = z;
        self->mDirty.set(Velocity);
    }
    else
    {
        self->mImmediate.vVelocity.x = x;
        self->mImmediate.vVelocity.y = y;
        self->mImmediate.vVelocity.z = z;

        alListener3fDirect(self->mContext, AL_VELOCITY, x, y, -z);
        alGetErrorDirect(self->mContext);
    }

    return S_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "CommitDeferredSettings "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Listener3D::CommitDeferredSettings() noexcept
{
    DEBUG("({})->()", voidp{this});

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};

    alcSuspendContext(self->mContext);
    self->commit();
    alcProcessContext(self->mContext);

    return DS_OK;
}
#undef PREFIX
#undef CLASS_PREFIX

/*** IKsPropertySet interface. ***/
#define CLASS_PREFIX "PrimaryProp::"
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Prop::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE PrimaryBuffer::Prop::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mPropRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE PrimaryBuffer::Prop::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mPropRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Get "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Prop::Get(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData,
    ULONG *pcbReturned) noexcept
{
    FIXME("({})->({}, 0x{:x}, {}, {}, {}, {}, {}): stub!", voidp{this},
        PropidPrinter{guidPropSet}.c_str(), dwPropID, pInstanceData, cbInstanceData, pPropData,
        cbPropData, voidp{pcbReturned});

    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Set "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Prop::Set(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData) noexcept
{
    FIXME("({})->({}, 0x{:x}, {}, {}, {}, {}): stub!", voidp{this},
        PropidPrinter{guidPropSet}.c_str(), dwPropID, pInstanceData, cbInstanceData, pPropData,
        cbPropData);

    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "QuerySupport "
HRESULT STDMETHODCALLTYPE PrimaryBuffer::Prop::QuerySupport(REFGUID guidPropSet, ULONG dwPropID,
    ULONG *pTypeSupport) noexcept
{
    FIXME("({})->({}, 0x{:x}, {}): stub!", voidp{this}, PropidPrinter{guidPropSet}.c_str(),
        dwPropID, voidp{pTypeSupport});

    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX
#undef CLASS_PREFIX
