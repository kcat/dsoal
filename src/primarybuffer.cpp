#include "primarybuffer.h"

#include <ksmedia.h>
#include <mmreg.h>

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

PrimaryBuffer::PrimaryBuffer(DSound8OAL &parent) : mParent{parent} { }

PrimaryBuffer::~PrimaryBuffer() = default;


HRESULT STDMETHODCALLTYPE PrimaryBuffer::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG("PrimaryBuffer::QueryInterface (%p)->(%s, %p)\n", voidp{this}, GuidPrinter{riid}.c_str(),
        voidp{ppvObject});

    *ppvObject = NULL;
    if(riid == ds::IID_IUnknown)
    {
        AddRef();
        *ppvObject = as<IUnknown*>();
        return S_OK;
    }
    if(riid == ds::IID_IDirectSoundBuffer)
    {
        AddRef();
        *ppvObject = as<IDirectSoundBuffer*>();
        return S_OK;
    }

    FIXME("PrimaryBuffer::QueryInterface Unhandled GUID: %s\n", GuidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE PrimaryBuffer::AddRef() noexcept
{
    const auto ret = mRef.fetch_add(1u, std::memory_order_relaxed) + 1;

    /* Clear the flags when getting the first reference, so it can be
     * reinitialized.
     */
    if(ret == 1)
        mFlags = 0;

    DEBUG("PrimaryBuffer::AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE PrimaryBuffer::Release() noexcept
{
    /* The primary buffer is a static object and should not be deleted.
     *
     * NOTE: Some buggy apps try to release after hitting 0 references, so
     * prevent underflowing the reference counter.
     */
    ULONG ret{mRef.load(std::memory_order_relaxed)};
    do {
        if(ret == 0) UNLIKELY
        {
            WARN("PrimaryBuffer::Release (%p) ref already %lu\n", voidp{this}, ret);
            return ret;
        }
    } while(!mRef.compare_exchange_weak(ret, ret-1, std::memory_order_relaxed));
    ret -= 1;

    DEBUG("PrimaryBuffer::Release (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}


HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetCaps(DSBCAPS *bufferCaps) noexcept
{
    FIXME("PrimaryBuffer::GetCaps (%p)->(%p)\n", voidp{this}, voidp{bufferCaps});

    if(!bufferCaps || bufferCaps->dwSize < sizeof(*bufferCaps))
    {
        WARN("Invalid DSBCAPS (%p, %lu)\n", voidp{bufferCaps},
            bufferCaps ? bufferCaps->dwSize : 0lu);
        return DSERR_INVALIDPARAM;
    }

    bufferCaps->dwFlags = mFlags;
    bufferCaps->dwBufferBytes = PrimaryBufSize;
    bufferCaps->dwUnlockTransferRate = 0;
    bufferCaps->dwPlayCpuOverhead = 0;

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetCurrentPosition(DWORD *playCursor, DWORD *writeCursor) noexcept
{
    DEBUG("PrimaryBuffer::GetCurrentPosition (%p)->(%p, %p)\n", voidp{this}, voidp{playCursor},
        voidp{writeCursor});
    return DSERR_PRIOLEVELNEEDED;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetFormat(WAVEFORMATEX *wfx, DWORD sizeAllocated, DWORD *sizeWritten) noexcept
{
    DEBUG("PrimaryBuffer::GetFormat (%p)->(%p, %lu, %p)\n", voidp{this}, voidp{wfx},
        sizeAllocated, voidp{sizeWritten});

    if(!wfx && !sizeWritten)
    {
        WARN("PrimaryBuffer::GetFormat Cannot report format or format size\n");
        return DSERR_INVALIDPARAM;
    }

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

HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetVolume(LONG *volume) noexcept
{
    FIXME("PrimaryBuffer::GetVolume (%p)->(%p)\n", voidp{this}, voidp{volume});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetPan(LONG *pan) noexcept
{
    FIXME("PrimaryBuffer::GetPan (%p)->(%p)\n", voidp{this}, voidp{pan});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetFrequency(DWORD *frequency) noexcept
{
    DEBUG("PrimaryBuffer::GetFrequency (%p)->(%p)\n", voidp{this}, voidp{frequency});

    if(!frequency)
        return DSERR_INVALIDPARAM;

    if(!(mFlags&DSBCAPS_CTRLFREQUENCY))
        return DSERR_CONTROLUNAVAIL;

    *frequency = mFormat.Format.nSamplesPerSec;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetStatus(DWORD *status) noexcept
{
    DEBUG("PrimaryBuffer::GetStatus (%p)->(%p)\n", voidp{this}, voidp{status});

    if(!status)
        return DSERR_INVALIDPARAM;

    if(!mPlaying)
    {
        /* TODO: Should be reported as playing if any secondary buffers are
         * playing.
         */
        *status = 0;
    }
    else
    {
        *status = DSBSTATUS_PLAYING|DSBSTATUS_LOOPING;
        if((mFlags&DSBCAPS_LOCDEFER))
            *status |= DSBSTATUS_LOCHARDWARE;
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::Initialize(IDirectSound *directSound, const DSBUFFERDESC *dsBufferDesc) noexcept
{
    DEBUG("PrimaryBuffer::Initialize (%p)->(%p, %p)\n", voidp{this}, voidp{directSound},
        cvoidp{dsBufferDesc});

    if(!dsBufferDesc || dsBufferDesc->lpwfxFormat || dsBufferDesc->dwBufferBytes)
    {
        WARN("PrimaryBuffer::Initialize Bad DSBUFFERDESC\n");
        return DSERR_INVALIDPARAM;
    }

    static constexpr DWORD BadFlags{DSBCAPS_CTRLFX | DSBCAPS_CTRLPOSITIONNOTIFY
        | DSBCAPS_LOCSOFTWARE};
    if((dsBufferDesc->dwFlags&BadFlags))
    {
        WARN("Bad dwFlags %08lx\n", dsBufferDesc->dwFlags);
        return DSERR_INVALIDPARAM;
    }

    if(mFlags != 0)
        return DSERR_ALREADYINITIALIZED;

    mFlags = dsBufferDesc->dwFlags | DSBCAPS_LOCHARDWARE;

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::Lock(DWORD offset, DWORD bytes, void **audioPtr1, DWORD *audioBytes1, void **audioPtr2, DWORD *audioBytes2, DWORD flags) noexcept
{
    DEBUG("PrimaryBuffer::Lock (%p)->(%lu, %lu, %p, %p, %p, %p, %lu)\n", voidp{this}, offset,
        bytes, voidp{audioPtr1}, voidp{audioBytes1}, voidp{audioPtr2}, voidp{audioBytes2}, flags);
    return DSERR_PRIOLEVELNEEDED;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::Play(DWORD reserved1, DWORD reserved2, DWORD flags) noexcept
{
    DEBUG("PrimaryBuffer::Play (%p)->(%lu, %lu, %lu)\n", voidp{this}, reserved1, reserved2, flags);

    if(!(flags & DSBPLAY_LOOPING))
    {
        WARN("PrimaryBuffer::Play Flags (%08lx) not set to DSBPLAY_LOOPING\n", flags);
        return DSERR_INVALIDPARAM;
    }

    mPlaying = true;

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetCurrentPosition(DWORD newPosition) noexcept
{
    FIXME("PrimaryBuffer::SetCurrentPosition (%p)->(%lu)\n", voidp{this}, newPosition);
    return DSERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetFormat(const WAVEFORMATEX *wfx) noexcept
{
    DEBUG("PrimaryBuffer::SetFormat (%p)->(%p)\n", voidp{this}, cvoidp{wfx});

    if(!wfx)
    {
        WARN("PrimaryBuffer::SetFormat Missing format\n");
        return DSERR_INVALIDPARAM;
    }

    if(mParent.getPriorityLevel() < DSSCL_PRIORITY)
        return DSERR_PRIOLEVELNEEDED;

    static constexpr WORD ExtExtraSize{sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)};
    if(wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        /* Fail silently.. */
        if(wfx->cbSize < ExtExtraSize)
        {
            WARN("PrimaryBuffer::SetFormat EXTENSIBLE size too small (%u, expected %u). Ignoring...\n",
                wfx->cbSize, ExtExtraSize);
            return DS_OK;
        }

        const WAVEFORMATEXTENSIBLE *wfe{CONTAINING_RECORD(wfx, const WAVEFORMATEXTENSIBLE, Format)};
        TRACE("PrimaryBuffer::SetFormat Requested primary format:\n"
              "    FormatTag          = 0x%04x\n"
              "    Channels           = %u\n"
              "    SamplesPerSec      = %lu\n"
              "    AvgBytesPerSec     = %lu\n"
              "    BlockAlign         = %u\n"
              "    BitsPerSample      = %u\n"
              "    ValidBitsPerSample = %u\n"
              "    ChannelMask        = 0x%08lx\n"
              "    SubFormat          = %s\n",
            wfe->Format.wFormatTag, wfe->Format.nChannels, wfe->Format.nSamplesPerSec,
            wfe->Format.nAvgBytesPerSec, wfe->Format.nBlockAlign, wfe->Format.wBitsPerSample,
            wfe->Samples.wValidBitsPerSample, wfe->dwChannelMask,
            GuidPrinter{wfe->SubFormat}.c_str());
    }
    else
    {
        TRACE("PrimaryBuffer::SetFormat Requested primary format:\n"
              "    FormatTag      = 0x%04x\n"
              "    Channels       = %u\n"
              "    SamplesPerSec  = %lu\n"
              "    AvgBytesPerSec = %lu\n"
              "    BlockAlign     = %u\n"
              "    BitsPerSample  = %u\n",
            wfx->wFormatTag, wfx->nChannels, wfx->nSamplesPerSec, wfx->nAvgBytesPerSec,
            wfx->nBlockAlign, wfx->wBitsPerSample);
    }

    auto copy_format = [wfx](WAVEFORMATEXTENSIBLE &dst) -> HRESULT
    {
        if(wfx->nChannels <= 0)
        {
            WARN("copy_format Invalid Channels %d\n", wfx->nChannels);
            return DSERR_INVALIDPARAM;
        }
        if(wfx->nSamplesPerSec < DSBFREQUENCY_MIN || wfx->nSamplesPerSec > DSBFREQUENCY_MAX)
        {
            WARN("copy_format Invalid SamplesPerSec %lu\n", wfx->nSamplesPerSec);
            return DSERR_INVALIDPARAM;
        }
        if(wfx->nBlockAlign <= 0)
        {
            WARN("copy_format Invalid BlockAlign %d\n", wfx->nBlockAlign);
            return DSERR_INVALIDPARAM;
        }
        if(wfx->wBitsPerSample == 0 || (wfx->wBitsPerSample%8) != 0)
        {
            WARN("copy_format Invalid BitsPerSample %d\n", wfx->wBitsPerSample);
            return DSERR_INVALIDPARAM;
        }
        if(wfx->nBlockAlign != wfx->nChannels*wfx->wBitsPerSample/8)
        {
            WARN("copy_format Invalid BlockAlign %d (expected %u = %u*%u/8)\n",
                 wfx->nBlockAlign, wfx->nChannels*wfx->wBitsPerSample/8,
                 wfx->nChannels, wfx->wBitsPerSample);
            return DSERR_INVALIDPARAM;
        }
        if(wfx->nAvgBytesPerSec != wfx->nBlockAlign*wfx->nSamplesPerSec)
        {
            WARN("copy_format Invalid AvgBytesPerSec %lu (expected %lu = %lu*%u)\n",
                 wfx->nAvgBytesPerSec, wfx->nSamplesPerSec*wfx->nBlockAlign,
                 wfx->nSamplesPerSec, wfx->nBlockAlign);
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
            const WAVEFORMATEXTENSIBLE *fromx{CONTAINING_RECORD(wfx, const WAVEFORMATEXTENSIBLE, Format)};

            if(fromx->Samples.wValidBitsPerSample > fromx->Format.wBitsPerSample)
                return DSERR_INVALIDPARAM;

            if(fromx->SubFormat == ds::KSDATAFORMAT_SUBTYPE_PCM)
            {
                if(wfx->wBitsPerSample > 32)
                    return DSERR_INVALIDPARAM;
            }
            else if(fromx->SubFormat == ds::KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            {
                if(wfx->wBitsPerSample != 32)
                    return DSERR_INVALIDPARAM;
            }
            else
            {
                FIXME("copy_format Unhandled extensible format: %s\n",
                    GuidPrinter{fromx->SubFormat}.c_str());
                return DSERR_INVALIDPARAM;
            }

            dst = {};
            dst.Format.cbSize = ExtExtraSize;
            dst.Samples.wValidBitsPerSample = fromx->Samples.wValidBitsPerSample;
            if(!dst.Samples.wValidBitsPerSample)
                dst.Samples.wValidBitsPerSample = fromx->Format.wBitsPerSample;
            dst.dwChannelMask = fromx->dwChannelMask;
            dst.SubFormat = fromx->SubFormat;
        }
        else
        {
            FIXME("copy_format Unhandled format tag %04x\n", wfx->wFormatTag);
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

    return copy_format(mFormat);
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetVolume(LONG volume) noexcept
{
    FIXME("PrimaryBuffer::SetVolume (%p)->(%ld)\n", voidp{this}, volume);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetPan(LONG pan) noexcept
{
    FIXME("PrimaryBuffer::SetPan (%p)->(%ld)\n", voidp{this}, pan);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetFrequency(DWORD frequency) noexcept
{
    FIXME("PrimaryBuffer::SetFrequency (%p)->(%lu)\n", voidp{this}, frequency);
    return DSERR_CONTROLUNAVAIL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::Stop() noexcept
{
    DEBUG("PrimaryBuffer::Stop (%p)->()\n", voidp{this});

    mPlaying = false;

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::Unlock(void *audioPtr1, DWORD audioBytes1, void *audioPtr2, DWORD audioBytes2) noexcept
{
    FIXME("PrimaryBuffer::Unlock (%p)->(%p, %lu, %p, %lu)\n", voidp{this}, audioPtr1, audioBytes1, audioPtr2, audioBytes2);
    return DSERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::Restore() noexcept
{
    FIXME("PrimaryBuffer::Restore (%p)->()\n", voidp{this});
    return DS_OK;
}
