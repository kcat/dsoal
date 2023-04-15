#include "primarybuffer.h"

#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

} // namespace

PrimaryBuffer::PrimaryBuffer() = default;

PrimaryBuffer::~PrimaryBuffer() = default;


HRESULT STDMETHODCALLTYPE PrimaryBuffer::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG("PrimaryBuffer::QueryInterface (%p)->(%s, %p)\n", voidp{this}, GuidPrinter{riid}.c_str(),
        voidp{ppvObject});

    *ppvObject = NULL;
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
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetCurrentPosition(DWORD *playCursor, DWORD *writeCursor) noexcept
{
    FIXME("PrimaryBuffer::GetCurrentPosition (%p)->(%p, %p)\n", voidp{this}, voidp{playCursor},
        voidp{writeCursor});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetFormat(WAVEFORMATEX *wfxFormat, DWORD sizeAllocated, DWORD *sizeWritten) noexcept
{
    FIXME("PrimaryBuffer::GetFormat (%p)->(%p, %lu, %p)\n", voidp{this}, voidp{wfxFormat},
        sizeAllocated, voidp{sizeWritten});
    return E_NOTIMPL;
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
    FIXME("PrimaryBuffer::GetFrequency (%p)->(%p)\n", voidp{this}, voidp{frequency});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::GetStatus(DWORD *status) noexcept
{
    FIXME("PrimaryBuffer::GetStatus (%p)->(%p)\n", voidp{this}, voidp{status});
    return E_NOTIMPL;
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
    FIXME("PrimaryBuffer::Lock (%p)->(%lu, %lu, %p, %p, %p, %p, %lu)\n", voidp{this}, offset,
        bytes, voidp{audioPtr1}, voidp{audioBytes1}, voidp{audioPtr2}, voidp{audioBytes2}, flags);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::Play(DWORD reserved1, DWORD reserved2, DWORD flags) noexcept
{
    FIXME("PrimaryBuffer::Play (%p)->(%lu, %lu, %lu)\n", voidp{this}, reserved1, reserved2, flags);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetCurrentPosition(DWORD newPosition) noexcept
{
    FIXME("PrimaryBuffer::SetCurrentPosition (%p)->(%lu)\n", voidp{this}, newPosition);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::SetFormat(const WAVEFORMATEX *wfxFormat) noexcept
{
    FIXME("PrimaryBuffer::SetFormat (%p)->(%p)\n", voidp{this}, cvoidp{wfxFormat});
    return E_NOTIMPL;
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
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::Stop() noexcept
{
    FIXME("PrimaryBuffer::Stop (%p)->()\n", voidp{this});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::Unlock(void *audioPtr1, DWORD audioBytes1, void *audioPtr2, DWORD audioBytes2) noexcept
{
    FIXME("PrimaryBuffer::Unlock (%p)->(%p, %lu, %p, %lu)\n", voidp{this}, audioPtr1, audioBytes1, audioPtr2, audioBytes2);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PrimaryBuffer::Restore() noexcept
{
    FIXME("PrimaryBuffer::Restore (%p)->()\n", voidp{this});
    return E_NOTIMPL;
}
