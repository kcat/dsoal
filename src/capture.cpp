#include "capture.h"

#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

} // namespace

#define PREFIX "DSCapture::"
ComPtr<DSCapture> DSCapture::Create(bool is8)
{
    return ComPtr<DSCapture>{new DSCapture{is8}};
}

DSCapture::DSCapture(bool is8) : mIs8{is8} { }
DSCapture::~DSCapture() = default;


HRESULT STDMETHODCALLTYPE DSCapture::QueryInterface(REFIID riid, void **ppvObject) noexcept
{
    DEBUG(PREFIX "QueryInterface (%p)->(%s, %p)\n", voidp{this}, GuidPrinter{riid}.c_str(),
        voidp{ppvObject});

    *ppvObject = nullptr;
    if(riid == IID_IUnknown)
    {
        mUnknownIface.AddRef();
        *ppvObject = mUnknownIface.as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundCapture)
    {
        AddRef();
        *ppvObject = as<IDirectSoundCapture*>();
        return S_OK;
    }

    FIXME(PREFIX "QueryInterface Unhandled GUID: %s\n", GuidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE DSCapture::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSCapture::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        delete this;
    return ret;
}

HRESULT STDMETHODCALLTYPE DSCapture::CreateCaptureBuffer(const DSCBUFFERDESC *dscBufferDesc,
    IDirectSoundCaptureBuffer **dsCaptureBuffer, IUnknown *unk) noexcept
{
    FIXME(PREFIX "CreateCaptureBuffer (%p)->(%p, %p, %p)\n", voidp{this}, cvoidp{dscBufferDesc},
        voidp{dsCaptureBuffer}, voidp{unk});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSCapture::GetCaps(DSCCAPS *dscCaps) noexcept
{
    FIXME(PREFIX "GetCaps (%p)->(%p)\n", voidp{this}, voidp{dscCaps});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSCapture::Initialize(const GUID *guid) noexcept
{
    FIXME(PREFIX "Initialize (%p)->(%s)\n", voidp{this}, GuidPrinter{guid}.c_str());
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX "DSCapture::Unknown::"
HRESULT STDMETHODCALLTYPE DSCapture::Unknown::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE DSCapture::Unknown::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mUnkRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSCapture::Unknown::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mUnkRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        delete self;
    return ret;
}
#undef PREFIX
