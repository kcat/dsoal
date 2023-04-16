#include "buffer.h"

#include "dsoal.h"
#include "dsoundoal.h"
#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

ALenum ConvertFormat(WAVEFORMATEXTENSIBLE &dst, const WAVEFORMATEX &src) noexcept
{
    TRACE("ConvertFormat Requested buffer format:\n"
          "    FormatTag      = 0x%04x\n"
          "    Channels       = %d\n"
          "    SamplesPerSec  = %lu\n"
          "    AvgBytesPerSec = %lu\n"
          "    BlockAlign     = %d\n"
          "    BitsPerSample  = %d\n"
          "    Size           = %d\n",
        src.wFormatTag, src.nChannels, src.nSamplesPerSec, src.nAvgBytesPerSec, src.nBlockAlign,
        src.wBitsPerSample, src.cbSize);

    dst.Format = src;
    dst.Format.cbSize = 0;

    if(dst.Format.wFormatTag != WAVE_FORMAT_PCM)
    {
        FIXME("ConvertFormat Format 0x%04x samples not available\n", dst.Format.wFormatTag);
        return AL_NONE;
    }

    if(dst.Format.wBitsPerSample == 8)
    {
        switch(dst.Format.nChannels)
        {
        case 1: return AL_FORMAT_MONO8;
        case 2: return AL_FORMAT_STEREO8;
        }
    }
    else if(dst.Format.wBitsPerSample == 16)
    {
        switch(dst.Format.nChannels)
        {
        case 1: return AL_FORMAT_MONO16;
        case 2: return AL_FORMAT_STEREO16;
        }
    }

    FIXME("ConvertFormat Could not get OpenAL format (%d-bit, %d channels)\n",
          dst.Format.wBitsPerSample, dst.Format.nChannels);
    return AL_NONE;
}

} // namespace

#define PREFIX "SharedBuffer::"
SharedBuffer::~SharedBuffer()
{
    if(mAlBuffer != 0)
        alDeleteBuffers(1, &mAlBuffer);
}

void SharedBuffer::dispose() noexcept
{
    std::destroy_at(this);
    std::free(this);
}

ds::expected<ComPtr<SharedBuffer>,HRESULT> SharedBuffer::Create(const DSBUFFERDESC &bufferDesc) noexcept
{
    const WAVEFORMATEX *format{bufferDesc.lpwfxFormat};

    if(format->nChannels <= 0)
    {
        WARN(PREFIX "Create Invalid Channels %d\n", format->nChannels);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nSamplesPerSec < DSBFREQUENCY_MIN || format->nSamplesPerSec > DSBFREQUENCY_MAX)
    {
        WARN(PREFIX "Create Invalid SamplesPerSec %lu\n", format->nSamplesPerSec);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nBlockAlign <= 0)
    {
        WARN(PREFIX "Create Invalid BlockAlign %d\n", format->nBlockAlign);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->wBitsPerSample == 0 || (format->wBitsPerSample%8) != 0)
    {
        WARN(PREFIX "Create Invalid BitsPerSample %d\n", format->wBitsPerSample);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nBlockAlign != format->nChannels*format->wBitsPerSample/8)
    {
        WARN(PREFIX "Create Invalid BlockAlign %d (expected %u = %u*%u/8)\n",
             format->nBlockAlign, format->nChannels*format->wBitsPerSample/8,
             format->nChannels, format->wBitsPerSample);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    /* HACK: Some games provide an incorrect value here and expect to work.
     * This is clearly not supposed to succeed with just anything, but until
     * the amount of leeway allowed is discovered, be very lenient.
     */
    if(format->nAvgBytesPerSec == 0)
    {
        WARN(PREFIX "Create Invalid AvgBytesPerSec %lu (expected %lu = %lu*%u)\n",
            format->nAvgBytesPerSec, format->nSamplesPerSec*format->nBlockAlign,
            format->nSamplesPerSec, format->nBlockAlign);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nAvgBytesPerSec != format->nBlockAlign*format->nSamplesPerSec)
        WARN(PREFIX "Create Unexpected AvgBytesPerSec %lu (expected %lu = %lu*%u)\n",
            format->nAvgBytesPerSec, format->nSamplesPerSec*format->nBlockAlign,
            format->nSamplesPerSec, format->nBlockAlign);

    static constexpr DWORD LocFlags{DSBCAPS_LOCSOFTWARE | DSBCAPS_LOCHARDWARE};
    if((bufferDesc.dwFlags&LocFlags) == LocFlags)
    {
        WARN(PREFIX "Create Hardware and software location requested\n");
        return ds::unexpected(DSERR_INVALIDPARAM);
    }

    /* Round the buffer size up to the next black alignment. */
    DWORD bufSize{bufferDesc.dwBufferBytes + format->nBlockAlign - 1};
    bufSize -= bufSize%format->nBlockAlign;
    if(bufSize < DSBSIZE_MIN) return ds::unexpected(DSERR_BUFFERTOOSMALL);
    if(bufSize > DSBSIZE_MAX) return ds::unexpected(DSERR_INVALIDPARAM);

    /* Over-allocate the shared buffer, combining it with the sample storage. */
    void *storage{std::malloc(sizeof(SharedBuffer) + bufSize)};
    if(!storage) return ds::unexpected(DSERR_OUTOFMEMORY);

    auto shared = ComPtr<SharedBuffer>{::new(storage) SharedBuffer{}};
    shared->mData = reinterpret_cast<char*>(shared.get() + 1);
    shared->mDataSize = bufSize;
    shared->mFlags = bufferDesc.dwFlags;

    shared->mAlFormat = ConvertFormat(shared->mWfxFormat, *bufferDesc.lpwfxFormat);
    if(!shared->mAlFormat) return ds::unexpected(DSERR_INVALIDPARAM);

    alGenBuffers(1, &shared->mAlBuffer);
    alBufferData(shared->mAlBuffer, shared->mAlFormat, shared->mData,
        static_cast<ALsizei>(shared->mDataSize),
        static_cast<ALsizei>(shared->mWfxFormat.Format.nSamplesPerSec));
    alGetError();

    return shared;
}
#undef PREFIX

#define PREFIX "Buffer::"
Buffer::Buffer(DSound8OAL &parent, bool is8) noexcept
    : mParent{parent}, mMutex{parent.getMutex()}, mIs8{is8}
{
    mContext = parent.getShared().mContext.get();
}

Buffer::~Buffer()
{
    ALSection alsection{mContext};

    if(mSource != 0)
    {
        alDeleteSources(1, &mSource);
        alGetError();
        mSource = 0;
        if(mLocStatus == LocStatus::Hardware)
            mParent.getShared().decHwSources();
        else if(mLocStatus == LocStatus::Software)
            mParent.getShared().decSwSources();
    }

    if(mBuffer)
    {
        if((mBuffer->mFlags&DSBCAPS_CTRL3D))
            mParent.remove3dBuffer(this);
        mBuffer = nullptr;
    }
}


HRESULT Buffer::setLocation(LocStatus locStatus) noexcept
{
    if(locStatus != LocStatus::Any && mLocStatus == locStatus)
        return DS_OK;
    if(locStatus == LocStatus::Any && mLocStatus != LocStatus::None)
        return DS_OK;

    /* If we have a source, we're changing location, so return the source we
     * have to get a new one.
     */
    if(mSource != 0)
    {
        alDeleteSources(1, &mSource);
        mSource = 0;
        alGetError();

        if(mLocStatus == LocStatus::Hardware)
            mParent.getShared().decHwSources();
        else
            mParent.getShared().decSwSources();
    }
    mLocStatus = LocStatus::None;

    bool ok{false};
    if(locStatus != LocStatus::Software)
    {
        ok = mParent.getShared().incHwSources();
        if(ok) locStatus = LocStatus::Hardware;
    }
    if(locStatus != LocStatus::Hardware && !ok)
    {
        ok = mParent.getShared().incSwSources();
        if(ok) locStatus = LocStatus::Software;
    }
    if(!ok)
    {
        ERR("Out of %s sources\n",
            (locStatus == LocStatus::Hardware) ? "hardware" :
            (locStatus == LocStatus::Software) ? "software" : "any"
        );
        return DSERR_ALLOCATED;
    }

    alGenSources(1, &mSource);
    alSourcef(mSource, AL_GAIN, mB_to_gain(static_cast<float>(mVolume)));
    alSourcef(mSource, AL_PITCH, (mFrequency == 0) ? 1.0f :
        static_cast<float>(mFrequency)/static_cast<float>(mBuffer->mWfxFormat.Format.nSamplesPerSec));
    alGetError();

    if((mBuffer->mFlags&DSBCAPS_CTRL3D))
    {
        // FIXME: Set current 3D params
    }
    else
    {
        const ALfloat x{static_cast<ALfloat>(mPan-DSBPAN_LEFT)/(DSBPAN_RIGHT-DSBPAN_LEFT) - 0.5f};

        alSource3f(mSource, AL_POSITION, x, 0.0f, -std::sqrt(1.0f - x*x));
        alSourcef(mSource, AL_ROLLOFF_FACTOR, 0.0f);
        alSourcei(mSource, AL_SOURCE_RELATIVE, AL_TRUE);
        alGetError();
    }

    mLocStatus = locStatus;
    return DS_OK;
}


HRESULT STDMETHODCALLTYPE Buffer::QueryInterface(REFIID riid, void** ppvObject) noexcept
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
    if(riid == IID_IDirectSoundBuffer)
    {
        AddRef();
        *ppvObject = as<IDirectSoundBuffer*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundBuffer8)
    {
        if(!mIs8)
        {
            WARN(PREFIX "QueryInterface Requesting IDirectSoundBuffer8 iface for non-DS8 object\n");
            return E_NOINTERFACE;
        }
        AddRef();
        *ppvObject = as<IDirectSoundBuffer8*>();
        return S_OK;
    }
    if(riid == IID_IDirectSound3DBuffer)
    {
        if(!(mBuffer->mFlags&DSBCAPS_CTRL3D))
        {
            WARN(PREFIX "QueryInterface Requesting IDirectSound3DBuffer iface for non-3D object\n");
            return E_NOINTERFACE;
        }
        mBuffer3D.AddRef();
        *ppvObject = mBuffer3D.as<IDirectSound3DBuffer*>();
        return S_OK;
    }
    if(riid == IID_IKsPropertySet)
    {
        mProp.AddRef();
        *ppvObject = mProp.as<IKsPropertySet*>();
        return S_OK;
    }

    FIXME(PREFIX "QueryInterface Unhandled GUID: %s\n", GuidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE Buffer::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Buffer::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1)
        mParent.dispose(this);
    return ret;
}


HRESULT STDMETHODCALLTYPE Buffer::GetCaps(DSBCAPS *bufferCaps) noexcept
{
    DEBUG(PREFIX "GetCaps (%p)->(%p)\n", voidp{this}, voidp{bufferCaps});

    if(!bufferCaps || bufferCaps->dwSize < sizeof(*bufferCaps))
    {
        WARN(PREFIX "GetCaps Invalid DSBCAPS (%p, %lu)\n", voidp{bufferCaps},
            (bufferCaps ? bufferCaps->dwSize : 0));
        return DSERR_INVALIDPARAM;
    }

    bufferCaps->dwFlags = mBuffer->mFlags;
    if(!(mBuffer->mFlags&DSBCAPS_LOCDEFER))
    {
        if(mLocStatus == LocStatus::Hardware)
            bufferCaps->dwFlags |= DSBCAPS_LOCHARDWARE;
        else if(mLocStatus == LocStatus::Software)
            bufferCaps->dwFlags |= DSBCAPS_LOCSOFTWARE;
    }
    bufferCaps->dwBufferBytes = mBuffer->mDataSize;
    bufferCaps->dwUnlockTransferRate = 4096;
    bufferCaps->dwPlayCpuOverhead = 0;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetCurrentPosition(DWORD *playCursor, DWORD *writeCursor) noexcept
{
    DEBUG(PREFIX "GetCurrentPosition (%p)->(%p, %p)\n", voidp{this}, voidp{playCursor},
        voidp{writeCursor});

    ALint status{AL_INITIAL};
    ALint ofs{0};

    if(mSource != 0)
    {
        ALSection alsection{mContext};
        alGetSourcei(mSource, AL_BYTE_OFFSET, &ofs);
        alGetSourcei(mSource, AL_SOURCE_STATE, &status);
        alGetError();
    }

    auto &format = mBuffer->mWfxFormat.Format;
    DWORD pos, writecursor;
    if(status == AL_PLAYING)
    {
        pos = static_cast<ALuint>(ofs);
        writecursor = format.nSamplesPerSec / (1000 / 20);
        writecursor *= format.nBlockAlign;
    }
    else
    {
        /* AL_STOPPED means the source naturally reached its end, where
         * DirectSound's position should be at the end (OpenAL reports 0
         * for stopped sources). The Stop method correlates to pausing,
         * which would put the source into an AL_PAUSED state and correctly
         * hold its current position. AL_INITIAL means the buffer hasn't
         * been played since last changing location.
         */
        switch(status)
        {
            case AL_STOPPED: pos = mBuffer->mDataSize; break;
            case AL_PAUSED: pos = static_cast<ALuint>(ofs); break;
            default: pos = 0;
        }
        writecursor = 0;
    }

    if(pos > mBuffer->mDataSize)
    {
        ERR(PREFIX "GetCurrentPosition playpos > buf_size\n");
        pos %= mBuffer->mDataSize;
    }
    writecursor = (writecursor+pos) % mBuffer->mDataSize;

    DEBUG(PREFIX "GetCurrentPosition pos = %lu, write pos = %lu\n", pos, writecursor);

    if(playCursor) *playCursor = pos;
    if(writeCursor)  *writeCursor = writecursor;

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetFormat(WAVEFORMATEX *wfx, DWORD sizeAllocated, DWORD *sizeWritten) noexcept
{
    FIXME(PREFIX "GetFormat (%p)->(%p, %lu, %p)\n", voidp{this}, voidp{wfx},
        sizeAllocated, voidp{sizeWritten});

    if(!wfx && !sizeWritten)
    {
        WARN(PREFIX "GetFormat Cannot report format or format size\n");
        return DSERR_INVALIDPARAM;
    }

    const DWORD size{sizeof(mBuffer->mWfxFormat.Format) + mBuffer->mWfxFormat.Format.cbSize};
    if(sizeWritten)
        *sizeWritten = size;
    if(wfx)
    {
        if(sizeAllocated < size)
            return DSERR_INVALIDPARAM;
        std::memcpy(wfx, &mBuffer->mWfxFormat.Format, size);
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetVolume(LONG *volume) noexcept
{
    DEBUG(PREFIX "GetVolume (%p)->(%p)\n", voidp{this}, voidp{volume});

    if(!volume)
        return DSERR_INVALIDPARAM;

    if(!(mBuffer->mFlags&DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;

    *volume = mVolume;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetPan(LONG *pan) noexcept
{
    DEBUG(PREFIX "GetPan (%p)->(%p)\n", voidp{this}, voidp{pan});

    if(!pan)
        return DSERR_INVALIDPARAM;

    if(!(mBuffer->mFlags&DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;

    *pan = mPan;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetFrequency(DWORD *frequency) noexcept
{
    FIXME(PREFIX "GetFrequency (%p)->(%p)\n", voidp{this}, voidp{frequency});

    if(!frequency)
        return DSERR_INVALIDPARAM;

    if(!(mBuffer->mFlags&DSBCAPS_CTRLFREQUENCY))
        return DSERR_CONTROLUNAVAIL;

    *frequency = mFrequency;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetStatus(DWORD *status) noexcept
{
    DEBUG(PREFIX "GetStatus (%p)->(%p)\n", voidp{this}, voidp{status});

    if(!status)
        return DSERR_INVALIDPARAM;
    *status = 0;

    ALint state{AL_INITIAL};
    ALint looping{AL_FALSE};
    if(mSource != 0)
    {
        ALSection alsection{mContext};
        alGetSourcei(mSource, AL_SOURCE_STATE, &state);
        alGetSourcei(mSource, AL_LOOPING, &looping);
        alGetError();
    }

    if((mBuffer->mFlags&DSBCAPS_LOCDEFER))
        *status |= static_cast<DWORD>(mLocStatus);
    if(state == AL_PLAYING)
        *status |= DSBSTATUS_PLAYING | (looping ? DSBSTATUS_LOOPING : 0);

    DEBUG(PREFIX "GetStatus status = 0x%08lx\n", *status);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Initialize(IDirectSound *directSound, const DSBUFFERDESC *dsBufferDesc) noexcept
{
    DEBUG("Buffer::Initialize (%p)->(%p, %p)\n", voidp{this}, voidp{directSound},
        cvoidp{dsBufferDesc});

    std::unique_lock lock{mMutex};
    if(mIsInitialized) return DSERR_ALREADYINITIALIZED;

    ALSection alsection{mContext};
    if(!mBuffer)
    {
        if(!dsBufferDesc)
        {
            WARN(PREFIX "Initialize Missing buffer description\n");
            return DSERR_INVALIDPARAM;
        }
        if(!dsBufferDesc->lpwfxFormat)
        {
            WARN(PREFIX "Initialize Missing buffer format\n");
            return DSERR_INVALIDPARAM;
        }
        if((dsBufferDesc->dwFlags&DSBCAPS_CTRL3D) && dsBufferDesc->lpwfxFormat->nChannels != 1)
        {
            if(mIs8)
            {
                /* DirectSoundBuffer8 objects aren't allowed non-mono 3D
                 * buffers.
                 */
                WARN(PREFIX "Initialize Can't create multi-channel 3D buffers\n");
                return DSERR_INVALIDPARAM;
            }
            else
            {
                static bool once{};
                if(!once)
                {
                    once = true;
                    ERR(PREFIX "Initialize Multi-channel 3D sounds are not spatialized\n");
                }
            }
        }
        if((dsBufferDesc->dwFlags&DSBCAPS_CTRLPAN) && dsBufferDesc->lpwfxFormat->nChannels != 1)
        {
            static bool once{false};
            if(!once)
            {
                once = true;
                ERR(PREFIX "Initialize Panning for multi-channel buffers is not supported\n");
            }
        }

        auto shared = SharedBuffer::Create(*dsBufferDesc);
        if(!shared) return shared.error();
        mBuffer = std::move(shared.value());
    }

    mVolume = 0;
    mPan = 0;
    mFrequency = mBuffer->mWfxFormat.Format.nSamplesPerSec;

    if((mBuffer->mFlags&DSBCAPS_CTRL3D))
        mParent.add3dBuffer(this);

    HRESULT hr{DS_OK};
    if(!(mBuffer->mFlags&DSBCAPS_LOCDEFER))
    {
        LocStatus locStatus{LocStatus::Any};
        if((mBuffer->mFlags&DSBCAPS_LOCHARDWARE)) locStatus = LocStatus::Hardware;
        else if((mBuffer->mFlags&DSBCAPS_LOCSOFTWARE)) locStatus = LocStatus::Software;
        hr = setLocation(locStatus);
    }
    mIsInitialized = SUCCEEDED(hr);

    return hr;
}

HRESULT STDMETHODCALLTYPE Buffer::Lock(DWORD offset, DWORD bytes, void **audioPtr1, DWORD *audioBytes1, void **audioPtr2, DWORD *audioBytes2, DWORD flags) noexcept
{
    FIXME(PREFIX "Lock (%p)->(%lu, %lu, %p, %p, %p, %p, %lu)\n", voidp{this}, offset, bytes,
        voidp{audioPtr1}, voidp{audioBytes1}, voidp{audioPtr2}, voidp{audioBytes2}, flags);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Play(DWORD reserved1, DWORD reserved2, DWORD flags) noexcept
{
    FIXME("Buffer::Play (%p)->(%lu, %lu, %lu)\n", voidp{this}, reserved1, reserved2, flags);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::SetCurrentPosition(DWORD newPosition) noexcept
{
    FIXME("Buffer::SetCurrentPosition (%p)->(%lu)\n", voidp{this}, newPosition);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::SetFormat(const WAVEFORMATEX *wfx) noexcept
{
    DEBUG("Buffer::SetFormat (%p)->(%p)\n", voidp{this}, cvoidp{wfx});
    return DSERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE Buffer::SetVolume(LONG volume) noexcept
{
    FIXME("Buffer::SetVolume (%p)->(%ld)\n", voidp{this}, volume);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::SetPan(LONG pan) noexcept
{
    FIXME("Buffer::SetPan (%p)->(%ld): stub\n", voidp{this}, pan);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::SetFrequency(DWORD frequency) noexcept
{
    FIXME("Buffer::SetFrequency (%p)->(%lu)\n", voidp{this}, frequency);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Stop() noexcept
{
    FIXME("Buffer::Stop (%p)->()\n", voidp{this});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Unlock(void *audioPtr1, DWORD audioBytes1, void *audioPtr2, DWORD audioBytes2) noexcept
{
    FIXME("Buffer::Unlock (%p)->(%p, %lu, %p, %lu)\n", voidp{this}, audioPtr1, audioBytes1, audioPtr2, audioBytes2);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Restore() noexcept
{
    FIXME("Buffer::Restore (%p)->()\n", voidp{this});
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::SetFX(DWORD effectsCount, DSEFFECTDESC *dsFXDesc, DWORD *resultCodes) noexcept
{
    FIXME("Buffer::SetFX (%p)->(%lu, %p, %p)\n", voidp{this}, effectsCount, voidp{dsFXDesc},
        voidp{resultCodes});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::AcquireResources(DWORD flags, DWORD effectsCount, DWORD *resultCodes) noexcept
{
    FIXME("Buffer::AcquireResources (%p)->(%lu, %lu, %p)\n", voidp{this}, flags, effectsCount,
        voidp{resultCodes});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetObjectInPath(REFGUID objectId, DWORD index, REFGUID interfaceId, void **ppObject) noexcept
{
    FIXME("Buffer::GetObjectInPath (%p)->(%s, %lu, %s, %p)\n", voidp{this},
        GuidPrinter{objectId}.c_str(), index, GuidPrinter{interfaceId}.c_str(), voidp{ppObject});
    return E_NOTIMPL;
}
#undef PREFIX

/*** IDirectSoundBuffer3D interface. ***/
#define PREFIX "Buffer3D::"
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE Buffer::Buffer3D::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mDs3dRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Buffer::Buffer3D::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mDs3dRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        self->mParent.dispose(self);
    return ret;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetAllParameters(DS3DBUFFER *ds3dBuffer) noexcept
{
    FIXME(PREFIX "GetAllParameters (%p)->(%p)\n", voidp{this}, voidp{ds3dBuffer});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetConeAngles(DWORD *insideConeAngle, DWORD *outsideConeAngle) noexcept
{
    FIXME(PREFIX "GetConeAngles (%p)->(%p, %p)\n", voidp{this}, voidp{insideConeAngle},
        voidp{outsideConeAngle});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetConeOrientation(D3DVECTOR *orientation) noexcept
{
    FIXME(PREFIX "GetConeOrientation (%p)->(%p)\n", voidp{this}, voidp{orientation});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetConeOutsideVolume(LONG *coneOutsideVolume) noexcept
{
    FIXME(PREFIX "GetConeOutsideVolume (%p)->(%p)\n", voidp{this}, voidp{coneOutsideVolume});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetMaxDistance(D3DVALUE *maxDistance) noexcept
{
    FIXME(PREFIX "GetMaxDistance (%p)->(%p)\n", voidp{this}, voidp{maxDistance});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetMinDistance(D3DVALUE *minDistance) noexcept
{
    FIXME(PREFIX "GetMinDistance (%p)->(%p)\n", voidp{this}, voidp{minDistance});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetMode(DWORD *mode) noexcept
{
    FIXME(PREFIX "GetMode (%p)->(%p)\n", voidp{this}, voidp{mode});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetPosition(D3DVECTOR *position) noexcept
{
    FIXME(PREFIX "GetPosition (%p)->(%p)\n", voidp{this}, voidp{position});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetVelocity(D3DVECTOR *velocity) noexcept
{
    FIXME(PREFIX "GetVelocity (%p)->(%p)\n", voidp{this}, voidp{velocity});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetAllParameters(const DS3DBUFFER *ds3dBuffer, DWORD apply) noexcept
{
    FIXME(PREFIX "SetAllParameters (%p)->(%p, %lu)\n", voidp{this}, cvoidp{ds3dBuffer}, apply);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetConeAngles(DWORD insideConeAngle, DWORD outsideConeAngle, DWORD apply) noexcept
{
    FIXME(PREFIX "SetConeAngles (%p)->(%lu, %lu, %lu)\n", voidp{this}, insideConeAngle,
        outsideConeAngle, apply);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetConeOrientation(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    FIXME(PREFIX "SetConeOrientation (%p)->(%f, %f, %f, %lu)\n", voidp{this}, x, y, z, apply);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetConeOutsideVolume(LONG coneOutsideVolume, DWORD apply) noexcept
{
    FIXME(PREFIX "SetConeOutsideVolume (%p)->(%ld, %lu)\n", voidp{this}, coneOutsideVolume, apply);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetMaxDistance(D3DVALUE maxDistance, DWORD apply) noexcept
{
    FIXME(PREFIX "SetMaxDistance (%p)->(%f, %lu)\n", voidp{this}, maxDistance, apply);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetMinDistance(D3DVALUE minDistance, DWORD apply) noexcept
{
    FIXME(PREFIX "SetMinDistace (%p)->(%f, %lu)\n", voidp{this}, minDistance, apply);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetMode(DWORD mode, DWORD apply) noexcept
{
    FIXME(PREFIX "SetMode (%p)->(%lu, %lu)\n", voidp{this}, mode, apply);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    FIXME(PREFIX "SetPosition (%p)->(%f, %f, %f, %lu)\n", voidp{this}, x, y, z, apply);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    FIXME(PREFIX "SetVelocity (%p)->(%f, %f, %f, %lu)\n", voidp{this}, x, y, z, apply);
    return E_NOTIMPL;
}
#undef PREFIX

/*** IDirectSoundBuffer3D interface. ***/
#define PREFIX "BufferProp::"
HRESULT STDMETHODCALLTYPE Buffer::Prop::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE Buffer::Prop::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mPropRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Buffer::Prop::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mPropRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        self->mParent.dispose(self);
    return ret;
}

HRESULT STDMETHODCALLTYPE Buffer::Prop::Get(REFGUID guidPropSet, ULONG dwPropID, void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData, ULONG *pcbReturned) noexcept
{
    FIXME(PREFIX "Get (%p)->(%s, 0x%lx, %p, %lu, %p, %lu, %p)\n", voidp{this},
        GuidPrinter{guidPropSet}.c_str(), dwPropID, pInstanceData, cbInstanceData, pPropData,
        cbPropData, voidp{pcbReturned});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Prop::Set(REFGUID guidPropSet, ULONG dwPropID, void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData) noexcept
{
    FIXME(PREFIX "Set (%p)->(%s, 0x%lx, %p, %lu, %p, %lu)\n", voidp{this},
        GuidPrinter{guidPropSet}.c_str(), dwPropID, pInstanceData, cbInstanceData, pPropData,
        cbPropData);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Prop::QuerySupport(REFGUID guidPropSet, ULONG dwPropID, ULONG *pTypeSupport) noexcept
{
    FIXME(PREFIX "QuerySupport (%p)->(%s, 0x%lx, %p)\n", voidp{this},
        GuidPrinter{guidPropSet}.c_str(), dwPropID, voidp{pTypeSupport});
    return E_NOTIMPL;
}
#undef PREFIX


/*** IUnknown interface wrapper. ***/
HRESULT STDMETHODCALLTYPE Buffer::UnknownImpl::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE Buffer::UnknownImpl::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mUnkRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("Buffer::UnknownImpl::AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Buffer::UnknownImpl::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mUnkRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("Buffer::UnknownImpl::Release (%p) ref %lu\n", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        self->mParent.dispose(self);
    return ret;
}
