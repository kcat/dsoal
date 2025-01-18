#include "capture.h"

#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

} // namespace

#define CLASS_PREFIX "DSCapture::"
ComPtr<DSCapture> DSCapture::Create(bool is8)
{
    return ComPtr<DSCapture>{new DSCapture{is8}};
}

DSCapture::DSCapture(bool is8) : mIs8{is8} { }
DSCapture::~DSCapture() = default;


#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE DSCapture::QueryInterface(REFIID riid, void **ppvObject) noexcept
{
    DEBUG(PREFIX "({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(),
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

    FIXME(PREFIX "Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSCapture::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSCapture::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "({}) ref {}", voidp{this}, ret);
    if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        delete this;
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "CreateCaptureBuffer "
HRESULT STDMETHODCALLTYPE DSCapture::CreateCaptureBuffer(const DSCBUFFERDESC *dscBufferDesc,
    IDirectSoundCaptureBuffer **dsCaptureBuffer, IUnknown *unk) noexcept
{
    FIXME(PREFIX "({})->({}, {}, {})", voidp{this}, cvoidp{dscBufferDesc},
        voidp{dsCaptureBuffer}, voidp{unk});
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetCaps "
HRESULT STDMETHODCALLTYPE DSCapture::GetCaps(DSCCAPS *dscCaps) noexcept
{
    TRACE(PREFIX "({})->({})", voidp{this}, voidp{dscCaps});

    auto dlock = std::unique_lock{mMutex};
    if(mDeviceName.empty())
    {
        WARN(PREFIX "Device not initialized");
        return DSERR_UNINITIALIZED;
    }

    if(!dscCaps)
    {
        WARN(PREFIX "Caps is null");
        return DSERR_INVALIDPARAM;
    }
    if(dscCaps->dwSize < sizeof(*dscCaps))
    {
        WARN(PREFIX "Invalid size: {}", dscCaps->dwSize);
        return DSERR_INVALIDPARAM;
    }

    dscCaps->dwFlags = 0;
    /* Support all WAVE_FORMAT formats specified in mmsystem.h */
    dscCaps->dwFormats = 0x000fffff;
    dscCaps->dwChannels = 2;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Initialize "
HRESULT STDMETHODCALLTYPE DSCapture::Initialize(const GUID *guid) noexcept
{
    TRACE(PREFIX "({})->({})", voidp{this}, DevidPrinter{guid}.c_str());

    auto dlock = std::unique_lock{mMutex};
    if(!mDeviceName.empty())
    {
        WARN(PREFIX "Already initialized");
        return DSERR_ALREADYINITIALIZED;
    }

    if(!guid || *guid == GUID_NULL)
        guid = &DSDEVID_DefaultCapture;
    else if(*guid == DSDEVID_DefaultPlayback || *guid == DSDEVID_DefaultVoicePlayback)
        return DSERR_NODRIVER;

    auto devguid = GUID{};
    auto hr = GetDeviceID(*guid, devguid);
    if(FAILED(hr)) return hr;

    {
        auto guid_str = LPOLESTR{};
        hr = StringFromCLSID(devguid, &guid_str);
        if(FAILED(hr))
        {
            ERR(PREFIX "Failed to convert GUID to string\n");
            return hr;
        }
        mDeviceName = wstr_to_utf8(guid_str);
        CoTaskMemFree(guid_str);
        guid_str = nullptr;
    }

    return DS_OK;
}
#undef PREFIX
#undef CLASS_PREFIX

#define CLASS_PREFIX "DSCapture::Unknown::"
HRESULT STDMETHODCALLTYPE DSCapture::Unknown::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSCapture::Unknown::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mUnkRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSCapture::Unknown::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mUnkRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "({}) ref {}", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        delete self;
    return ret;
}
#undef PREFIX
#undef CLASS_PREFIX
