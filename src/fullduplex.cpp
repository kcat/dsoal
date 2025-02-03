#include "fullduplex.h"

#include "capture.h"
#include "dsoundoal.h"
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
HRESULT STDMETHODCALLTYPE DSFullDuplex::QueryInterface(REFIID riid, void **ppvObject) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    if(!ppvObject)
        return E_POINTER;
    *ppvObject = nullptr;

    if(riid == IID_IUnknown)
    {
        mUnknownIface.AddRef();
        *ppvObject = mUnknownIface.as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundFullDuplex)
    {
        AddRef();
        *ppvObject = as<IDirectSoundFullDuplex*>();
        return S_OK;
    }
    if(riid == IID_IDirectSound8)
    {
        mDS8Iface.AddRef();
        *ppvObject = mDS8Iface.as<IDirectSound8*>();
        return S_OK;
    }
    if(riid == IID_IDirectSound)
    {
        mDS8Iface.AddRef();
        *ppvObject = mDS8Iface.as<IDirectSound*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundCapture)
    {
        mDSCIface.AddRef();
        *ppvObject = mDSCIface.as<IDirectSoundCapture*>();
        return S_OK;
    }

    FIXME("Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSFullDuplex::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mFdRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSFullDuplex::Release() noexcept
{
    const auto ret = mFdRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    finalize();
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Initialize "
HRESULT STDMETHODCALLTYPE DSFullDuplex::Initialize(const GUID *captureGuid, const GUID *renderGuid,
    const DSCBUFFERDESC *dscBufferDesc, const DSBUFFERDESC *dsBufferDesc, HWND hwnd, DWORD level,
    IDirectSoundCaptureBuffer8 **dsCaptureBuffer8, IDirectSoundBuffer8 **dsBuffer8) noexcept
{
    DEBUG("({})->({}, {}, {}, {}, {}, {}, {}, {})", voidp{this}, DevidPrinter{captureGuid}.c_str(),
        DevidPrinter{renderGuid}.c_str(), cvoidp{dscBufferDesc}, cvoidp{dsBufferDesc}, voidp{hwnd},
        level, voidp{dsCaptureBuffer8}, voidp{dsBuffer8});

    if(dsCaptureBuffer8) *dsCaptureBuffer8 = nullptr;
    if(dsBuffer8) *dsBuffer8 = nullptr;

    if(!dsCaptureBuffer8 || !dsBuffer8)
    {
        WARN("Null output pointers");
        return DSERR_INVALIDPARAM;
    }

    if(mDS8Handle || mDSCHandle)
    {
        WARN("Already initialized");
        return DSERR_ALREADYINITIALIZED;
    }

    try {
        mDS8Handle = DSound8OAL::Create(true);
        if(const auto hr = mDS8Handle->Initialize(renderGuid); FAILED(hr))
        {
            mDS8Handle = nullptr;
            return hr;
        }
    }
    catch(std::exception &e) {
        ERR("Exception creating IDirectSound8: {}", e.what());
        mDS8Handle = nullptr;
        return E_FAIL;
    }

    if(const auto hr = mDS8Handle->SetCooperativeLevel(hwnd, level); FAILED(hr))
    {
        mDS8Handle = nullptr;
        return hr;
    }

    auto dsbuf = ComPtr<IDirectSoundBuffer>{};
    if(auto hr = mDS8Handle->CreateSoundBuffer(dsBufferDesc, ds::out_ptr(dsbuf), nullptr);
        FAILED(hr))
    {
        mDS8Handle = nullptr;
        return hr;
    }

    try {
        mDSCHandle = DSCapture::Create(true);
        if(const auto hr = mDSCHandle->Initialize(captureGuid); FAILED(hr))
        {
            mDS8Handle = nullptr;
            mDSCHandle = nullptr;
            return hr;
        }
    }
    catch(std::exception &e) {
        ERR("Exception creating IDirectSoundCapture8: {}", e.what());
        mDS8Handle = nullptr;
        mDSCHandle = nullptr;
        return E_FAIL;
    }

    auto dscbuf = ComPtr<IDirectSoundCaptureBuffer>{};
    if(auto hr = mDSCHandle->CreateCaptureBuffer(dscBufferDesc, ds::out_ptr(dscbuf), nullptr);
        FAILED(hr))
    {
        mDS8Handle = nullptr;
        mDSCHandle = nullptr;
        return hr;
    }

    auto dsbuf8 = ComPtr<IDirectSoundBuffer8>{};
    if(auto hr = dsbuf->QueryInterface(IID_IDirectSoundBuffer8, ds::out_ptr(dsbuf8)); FAILED(hr))
        return hr;

    auto dscbuf8 = ComPtr<IDirectSoundCaptureBuffer8>{};
    if(auto hr = dscbuf->QueryInterface(IID_IDirectSoundCaptureBuffer8, ds::out_ptr(dscbuf8));
        FAILED(hr))
        return hr;

    *dsBuffer8 = dsbuf8.release();
    *dsCaptureBuffer8 = dscbuf8.release();

    return DS_OK;
}
#undef PREFIX
#undef CLASS_PREFIX

#define CLASS_PREFIX "DSFullDuplex::DS8::"
HRESULT STDMETHODCALLTYPE DSFullDuplex::DS8::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSFullDuplex::DS8::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mDS8Ref.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSFullDuplex::DS8::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mDS8Ref.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    self->finalize();
    return ret;
}
#undef PREFIX

HRESULT STDMETHODCALLTYPE DSFullDuplex::DS8::CreateSoundBuffer(const DSBUFFERDESC *bufferDesc, IDirectSoundBuffer **dsBuffer, IUnknown *outer) noexcept
{ return impl_from_base()->mDS8Handle->CreateSoundBuffer(bufferDesc, dsBuffer, outer); }

HRESULT STDMETHODCALLTYPE DSFullDuplex::DS8::GetCaps(DSCAPS *dsCaps) noexcept
{ return impl_from_base()->mDS8Handle->GetCaps(dsCaps); }

HRESULT STDMETHODCALLTYPE DSFullDuplex::DS8::DuplicateSoundBuffer(IDirectSoundBuffer *original, IDirectSoundBuffer **duplicate) noexcept
{ return impl_from_base()->mDS8Handle->DuplicateSoundBuffer(original, duplicate); }

HRESULT STDMETHODCALLTYPE DSFullDuplex::DS8::SetCooperativeLevel(HWND hwnd, DWORD level) noexcept
{ return impl_from_base()->mDS8Handle->SetCooperativeLevel(hwnd, level); }

HRESULT STDMETHODCALLTYPE DSFullDuplex::DS8::Compact() noexcept
{ return impl_from_base()->mDS8Handle->Compact(); }

HRESULT STDMETHODCALLTYPE DSFullDuplex::DS8::GetSpeakerConfig(DWORD *speakerConfig) noexcept
{ return impl_from_base()->mDS8Handle->GetSpeakerConfig(speakerConfig); }

HRESULT STDMETHODCALLTYPE DSFullDuplex::DS8::SetSpeakerConfig(DWORD speakerConfig) noexcept
{ return impl_from_base()->mDS8Handle->SetSpeakerConfig(speakerConfig); }

HRESULT STDMETHODCALLTYPE DSFullDuplex::DS8::Initialize(const GUID *deviceId) noexcept
{ return impl_from_base()->mDS8Handle->Initialize(deviceId); }

HRESULT STDMETHODCALLTYPE DSFullDuplex::DS8::VerifyCertification(DWORD *certified) noexcept
{ return impl_from_base()->mDS8Handle->VerifyCertification(certified); }
#undef CLASS_PREFIX

#define CLASS_PREFIX "DSFullDuplex::DSC::"
HRESULT STDMETHODCALLTYPE DSFullDuplex::DSC::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSFullDuplex::DSC::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mDSCRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSFullDuplex::DSC::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mDSCRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    self->finalize();
    return ret;
}
#undef PREFIX

HRESULT STDMETHODCALLTYPE DSFullDuplex::DSC::CreateCaptureBuffer(const DSCBUFFERDESC *dscBufferDesc, IDirectSoundCaptureBuffer **dsCaptureBuffer, IUnknown *unk) noexcept
{ return impl_from_base()->mDSCHandle->CreateCaptureBuffer(dscBufferDesc, dsCaptureBuffer, unk); }

HRESULT STDMETHODCALLTYPE DSFullDuplex::DSC::GetCaps(DSCCAPS *dscCaps) noexcept
{ return impl_from_base()->mDSCHandle->GetCaps(dscCaps); }

HRESULT STDMETHODCALLTYPE DSFullDuplex::DSC::Initialize(const GUID *guid) noexcept
{ return impl_from_base()->mDSCHandle->Initialize(guid); }
#undef CLASS_PREFIX

#define CLASS_PREFIX "DSFullDuplex::Unknown::"
HRESULT STDMETHODCALLTYPE DSFullDuplex::Unknown::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSFullDuplex::Unknown::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mUnkRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSFullDuplex::Unknown::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mUnkRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    self->finalize();
    return ret;
}
#undef PREFIX
#undef CLASS_PREFIX
