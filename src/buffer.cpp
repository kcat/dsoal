#include "buffer.h"

#include "dsoal.h"
#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

} // namespace

Buffer::Buffer(bool is8) : mIs8{is8} { }


HRESULT STDMETHODCALLTYPE Buffer::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG("Buffer::QueryInterface (%p)->(%s, %p)\n", voidp{this}, GuidPrinter{riid}.c_str(),
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
            WARN("Buffer::QueryInterface Requesting IDirectSoundBuffer8 iface for non-DS8 object\n");
            return E_NOINTERFACE;
        }
        AddRef();
        *ppvObject = as<IDirectSoundBuffer8*>();
        return S_OK;
    }

    FIXME("Buffer::QueryInterface Unhandled GUID: %s\n", GuidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE Buffer::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("Buffer::AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Buffer::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("Buffer::Release (%p) ref %lu\n", voidp{this}, ret);
    if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1)
        delete this;
    return ret;
}


HRESULT STDMETHODCALLTYPE Buffer::GetCaps(DSBCAPS *bufferCaps) noexcept
{
    FIXME("Buffer::GetCaps (%p)->(%p)\n", voidp{this}, voidp{bufferCaps});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetCurrentPosition(DWORD *playCursor, DWORD *writeCursor) noexcept
{
    FIXME("Buffer::GetCurrentPosition (%p)->(%p, %p)\n", voidp{this}, voidp{playCursor},
        voidp{writeCursor});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetFormat(WAVEFORMATEX *wfx, DWORD sizeAllocated, DWORD *sizeWritten) noexcept
{
    FIXME("Buffer::GetFormat (%p)->(%p, %lu, %p)\n", voidp{this}, voidp{wfx},
        sizeAllocated, voidp{sizeWritten});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetVolume(LONG *volume) noexcept
{
    FIXME("Buffer::GetVolume (%p)->(%p)\n", voidp{this}, voidp{volume});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetPan(LONG *pan) noexcept
{
    FIXME("Buffer::GetPan (%p)->(%p)\n", voidp{this}, voidp{pan});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetFrequency(DWORD *frequency) noexcept
{
    FIXME("Buffer::GetFrequency (%p)->(%p)\n", voidp{this}, voidp{frequency});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetStatus(DWORD *status) noexcept
{
    FIXME("Buffer::GetStatus (%p)->(%p)\n", voidp{this}, voidp{status});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::Initialize(IDirectSound *directSound, const DSBUFFERDESC *dsBufferDesc) noexcept
{
    FIXME("Buffer::Initialize (%p)->(%p, %p)\n", voidp{this}, voidp{directSound},
        cvoidp{dsBufferDesc});
    return E_NOTIMPL;
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

/*** IUnknown interface wrapper. ***/
HRESULT STDMETHODCALLTYPE Buffer::UnknownImpl::QueryInterface(REFIID riid, void **ppvObject) noexcept
{
    return impl_from_base()->QueryInterface(riid, ppvObject);
}

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
        delete self;
    return ret;
}
