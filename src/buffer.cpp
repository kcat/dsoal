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
    TRACE("Requested buffer format:\n"
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
Buffer::Buffer(DSound8OAL &parent, bool is8) : mParent{parent}, mIs8{is8}
{
    mContext = mParent.getShared().mContext.get();
}

Buffer::~Buffer()
{
    if(mContext)
    {
        ALSection alsection{mContext};

        if(mSource != 0u)
        {
            alDeleteSources(1, &mSource);
            alGetError();
            mSource = 0;
            if(mIsHardware)
                mParent.getShared().decHwSources();
            else
                mParent.getShared().decSwSources();
        }

        mBuffer = nullptr;
    }
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
    FIXME(PREFIX "GetCaps (%p)->(%p)\n", voidp{this}, voidp{bufferCaps});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetCurrentPosition(DWORD *playCursor, DWORD *writeCursor) noexcept
{
    FIXME(PREFIX "GetCurrentPosition (%p)->(%p, %p)\n", voidp{this}, voidp{playCursor},
        voidp{writeCursor});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetFormat(WAVEFORMATEX *wfx, DWORD sizeAllocated, DWORD *sizeWritten) noexcept
{
    FIXME(PREFIX "GetFormat (%p)->(%p, %lu, %p)\n", voidp{this}, voidp{wfx},
        sizeAllocated, voidp{sizeWritten});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetVolume(LONG *volume) noexcept
{
    FIXME(PREFIX "GetVolume (%p)->(%p)\n", voidp{this}, voidp{volume});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetPan(LONG *pan) noexcept
{
    FIXME(PREFIX "GetPan (%p)->(%p)\n", voidp{this}, voidp{pan});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetFrequency(DWORD *frequency) noexcept
{
    FIXME(PREFIX "GetFrequency (%p)->(%p)\n", voidp{this}, voidp{frequency});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetStatus(DWORD *status) noexcept
{
    FIXME(PREFIX "GetStatus (%p)->(%p)\n", voidp{this}, voidp{status});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Initialize(IDirectSound *directSound, const DSBUFFERDESC *dsBufferDesc) noexcept
{
    FIXME("Buffer::Initialize (%p)->(%p, %p)\n", voidp{this}, voidp{directSound},
        cvoidp{dsBufferDesc});

    std::unique_lock lock{mParent.getMutex()};
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

    if((mBuffer->mFlags&DSBCAPS_LOCDEFER))
    {
        FIXME(PREFIX "Initialize Deferred buffers not supported\n");
        return DSERR_INVALIDPARAM;
    }
    else
    {
        bool ok{false};
        if((mBuffer->mFlags&DSBCAPS_LOCHARDWARE))
        {
            ok = mParent.getShared().incHwSources();
            if(ok) mIsHardware = true;
        }
        else if((mBuffer->mFlags&DSBCAPS_LOCSOFTWARE))
        {
            ok = mParent.getShared().incSwSources();
            if(ok) mIsHardware = false;
        }
        else
        {
            ok = mParent.getShared().incHwSources();
            if(ok)
                mIsHardware = true;
            else
            {
                ok = mParent.getShared().incSwSources();
                if(ok) mIsHardware = false;
            }
        }

        alGetError();
        alGenSources(1, &mSource);
        if(alGetError() != AL_NO_ERROR)
        {
            if(mIsHardware)
                mParent.getShared().decHwSources();
            else
                mParent.getShared().decSwSources();
        }
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Lock(DWORD offset, DWORD bytes, void **audioPtr1, DWORD *audioBytes1, void **audioPtr2, DWORD *audioBytes2, DWORD flags) noexcept
{
    FIXME("Buffer::Lock (%p)->(%lu, %lu, %p, %p, %p, %p, %lu)\n", voidp{this}, offset,
        bytes, voidp{audioPtr1}, voidp{audioBytes1}, voidp{audioPtr2}, voidp{audioBytes2}, flags);
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
