#include "fullduplex.h"

#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

} // namespace

#define CLASS_PREFIX "DSFullDuplex::"
DSFullDuplex::DSFullDuplex() = default;
DSFullDuplex::~DSFullDuplex() = default;

ComPtr<DSFullDuplex> DSFullDuplex::Create()
{
    return ComPtr<DSFullDuplex>{new DSFullDuplex{}};
}


#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE DSFullDuplex::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG(PREFIX "(%p)->(%s, %p)\n", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    *ppvObject = nullptr;
    if(riid == IID_IUnknown)
    {
        AddRef();
        *ppvObject = as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundFullDuplex)
    {
        AddRef();
        *ppvObject = as<IDirectSoundFullDuplex*>();
        return S_OK;
    }

    FIXME(PREFIX "Unhandled GUID: %s\n", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

ULONG STDMETHODCALLTYPE DSFullDuplex::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mFdRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(CLASS_PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSFullDuplex::Release() noexcept
{
    const auto ret = mFdRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(CLASS_PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        delete this;
    return ret;
}

#define PREFIX CLASS_PREFIX "Initialize "
HRESULT STDMETHODCALLTYPE DSFullDuplex::Initialize(const GUID *captureGuid, const GUID *renderGuid,
    const DSCBUFFERDESC *dscBufferDesc, const DSBUFFERDESC *dsBufferDesc, HWND hwnd, DWORD level,
    IDirectSoundCaptureBuffer8 **dsCaptureBuffer8, IDirectSoundBuffer8 **dsBuffer8) noexcept
{
    DEBUG(PREFIX "(%p)->(%s, %s, %p, %p, %p, %lu, %p, %p)\n", voidp{this},
        DevidPrinter{captureGuid}.c_str(), DevidPrinter{renderGuid}.c_str(), cvoidp{dscBufferDesc},
          cvoidp{dsBufferDesc}, voidp{hwnd}, level, voidp{dsCaptureBuffer8}, voidp{dsBuffer8});
    return E_NOTIMPL;
}
#undef PREFIX
