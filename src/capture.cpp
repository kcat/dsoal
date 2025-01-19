#include "capture.h"

#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

class DSCBuffer final : IDirectSoundCaptureBuffer8 {
    explicit DSCBuffer(bool is8) : mIs8{is8} { }

    class Notify final : IDirectSoundNotify {
        auto impl_from_base() noexcept
        {
#ifdef __GNUC__
            _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wcast-align\"")
#endif
            return CONTAINING_RECORD(this, DSCBuffer, mNotify);
#ifdef __GNUC__
            _Pragma("GCC diagnostic pop")
#endif
        }

    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
        ULONG STDMETHODCALLTYPE AddRef() noexcept override;
        ULONG STDMETHODCALLTYPE Release() noexcept override;
        HRESULT STDMETHODCALLTYPE SetNotificationPositions(DWORD numNotifies, const DSBPOSITIONNOTIFY *notifies) noexcept override;

        Notify() = default;
        Notify(const Notify&) = delete;
        Notify& operator=(const Notify&) = delete;

        template<typename T>
        T as() noexcept { return static_cast<T>(this); }
    };
    Notify mNotify;

    std::atomic<ULONG> mTotalRef{1u}, mDsRef{1u}, mNotRef{0u};

    bool mIs8{};

public:
    ~DSCBuffer() = default;

    DSCBuffer(const DSCBuffer&) = delete;
    DSCBuffer& operator=(const DSCBuffer&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCBCAPS lpDSCBCaps) noexcept override;
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD lpdwCapturePosition,LPDWORD lpdwReadPosition) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD dwSizeAllocated, LPDWORD lpdwSizeWritten) noexcept override;
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD lpdwStatus) noexcept override;
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUNDCAPTURE lpDSC, LPCDSCBUFFERDESC lpcDSCBDesc) noexcept override;
    HRESULT STDMETHODCALLTYPE Lock(DWORD dwReadCusor, DWORD dwReadBytes, LPVOID *lplpvAudioPtr1, LPDWORD lpdwAudioBytes1, LPVOID *lplpvAudioPtr2, LPDWORD lpdwAudioBytes2, DWORD dwFlags) noexcept override;
    HRESULT STDMETHODCALLTYPE Start(DWORD dwFlags) noexcept override;
    HRESULT STDMETHODCALLTYPE Stop() noexcept override;
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID lpvAudioPtr1, DWORD dwAudioBytes1, LPVOID lpvAudioPtr2, DWORD dwAudioBytes2) noexcept override;
    HRESULT STDMETHODCALLTYPE GetObjectInPath(REFGUID rguidObject, DWORD dwIndex, REFGUID rguidInterface, LPVOID *ppObject) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFXStatus(DWORD dwFXCount, LPDWORD pdwFXStatus) noexcept override;

    template<typename T>
    T as() noexcept { return static_cast<T>(this); }

    static auto Create(bool is8) -> ComPtr<DSCBuffer>
    { return ComPtr<DSCBuffer>{new DSCBuffer{is8}}; }
};

#define CLASS_PREFIX "DSCBuffer::"

#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE DSCBuffer::QueryInterface(REFIID riid, void **ppvObject) noexcept
{
    DEBUG(PREFIX "({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    *ppvObject = nullptr;
    if(riid == IID_IUnknown)
    {
        AddRef();
        *ppvObject = as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundCaptureBuffer)
    {
        AddRef();
        *ppvObject = as<IDirectSoundCaptureBuffer*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundCaptureBuffer8)
    {
        if(!mIs8)
        {
            WARN(PREFIX "Requesting IDirectSoundCaptureBuffer8 iface for non-DS8 object");
            return E_NOINTERFACE;
        }
        AddRef();
        *ppvObject = as<IDirectSoundCaptureBuffer8*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundNotify)
    {
        mNotify.AddRef();
        *ppvObject = mNotify.as<IDirectSoundNotify*>();
        return S_OK;
    }

    FIXME(PREFIX "Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

ULONG STDMETHODCALLTYPE DSCBuffer::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(CLASS_PREFIX "AddRef ({}) ref {}", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSCBuffer::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(CLASS_PREFIX "Release ({}) ref {}", voidp{this}, ret);
    if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1)
        delete this;
    return ret;
}

#define PREFIX CLASS_PREFIX "GetCaps "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetCaps(LPDSCBCAPS lpDSCBCaps) noexcept
{
    FIXME(PREFIX "({})->({})", voidp{this}, voidp{lpDSCBCaps});
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetCurrentPosition "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetCurrentPosition(LPDWORD lpdwCapturePosition,
    LPDWORD lpdwReadPosition) noexcept
{
    FIXME(PREFIX "({})->({}, {})", voidp{this}, voidp{lpdwCapturePosition},
        voidp{lpdwReadPosition});
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetFormat "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD dwSizeAllocated,
    LPDWORD lpdwSizeWritten) noexcept
{
    FIXME(PREFIX "({})->({}, {}, {})", voidp{this}, voidp{lpwfxFormat}, dwSizeAllocated,
        voidp{lpdwSizeWritten});
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetStatus "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetStatus(LPDWORD lpdwStatus) noexcept
{
    FIXME(PREFIX "({})->({})", voidp{this}, voidp{lpdwStatus});
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Initialize "
HRESULT STDMETHODCALLTYPE DSCBuffer::Initialize(LPDIRECTSOUNDCAPTURE lpDSC,
    LPCDSCBUFFERDESC lpcDSCBDesc) noexcept
{
    FIXME(PREFIX "({})->({}, {})", voidp{this}, voidp{lpDSC}, cvoidp{lpcDSCBDesc});
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Lock "
HRESULT STDMETHODCALLTYPE DSCBuffer::Lock(DWORD dwReadCusor, DWORD dwReadBytes,
    LPVOID *lplpvAudioPtr1, LPDWORD lpdwAudioBytes1, LPVOID *lplpvAudioPtr2,
    LPDWORD lpdwAudioBytes2, DWORD dwFlags) noexcept
{
    FIXME(PREFIX "({})->({}, {}, {}, {}, {}, {}, {:#x})", voidp{this}, dwReadCusor, dwReadBytes,
        voidp{lplpvAudioPtr1}, voidp{lpdwAudioBytes1}, voidp{lplpvAudioPtr2},
        voidp{lpdwAudioBytes2}, dwFlags);
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Start "
HRESULT STDMETHODCALLTYPE DSCBuffer::Start(DWORD dwFlags) noexcept
{
    FIXME(PREFIX "({})->({:#x})", voidp{this}, dwFlags);
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Stop "
HRESULT STDMETHODCALLTYPE DSCBuffer::Stop() noexcept
{
    FIXME(PREFIX "({})->()", voidp{this});
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Unlock "
HRESULT STDMETHODCALLTYPE DSCBuffer::Unlock(LPVOID lpvAudioPtr1, DWORD dwAudioBytes1,
    LPVOID lpvAudioPtr2, DWORD dwAudioBytes2) noexcept
{
    FIXME(PREFIX "({})->({}, {}, {}, {})", voidp{this}, voidp{lpvAudioPtr1}, dwAudioBytes1,
        voidp{lpvAudioPtr2}, dwAudioBytes2);
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetObjectInPath "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetObjectInPath(REFGUID rguidObject, DWORD dwIndex,
    REFGUID rguidInterface, LPVOID *ppObject) noexcept
{
    FIXME(PREFIX "({})->({}, {}, {}, {})", voidp{this}, GuidPrinter{rguidObject}.c_str(), dwIndex,
        GuidPrinter{rguidInterface}.c_str(), voidp{ppObject});
    return E_NOTIMPL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetFXStatus "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetFXStatus(DWORD dwFXCount, LPDWORD pdwFXStatus) noexcept
{
    FIXME(PREFIX "({})->({}, {})", voidp{this}, dwFXCount, voidp{pdwFXStatus});
    return E_NOTIMPL;
}
#undef PREFIX
#undef CLASS_PREFIX

#define CLASS_PREFIX "DSCBuffer::Notify::"
HRESULT STDMETHODCALLTYPE DSCBuffer::Notify::QueryInterface(REFIID riid, void** ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE DSCBuffer::Notify::AddRef() noexcept
{
    auto *self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mNotRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(CLASS_PREFIX "AddRef ({}) ref {}", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSCBuffer::Notify::Release() noexcept
{
    auto *self = impl_from_base();
    const auto ret = self->mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(CLASS_PREFIX "Release ({}) ref {}", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1)
        delete this;
    return ret;
}

#define PREFIX CLASS_PREFIX "SetNotificationPositions "
HRESULT STDMETHODCALLTYPE DSCBuffer::Notify::SetNotificationPositions(DWORD numNotifies,
    const DSBPOSITIONNOTIFY *notifies) noexcept
{
    FIXME(PREFIX "({})->({}, {})", voidp{this}, numNotifies, cvoidp{notifies});
    return E_NOTIMPL;
}
#undef PREFIX
#undef CLASS_PREFIX

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
    DEBUG(PREFIX "({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

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
    IDirectSoundCaptureBuffer **dsCaptureBuffer, IUnknown *outer) noexcept
{
    TRACE(PREFIX "({})->({}, {}, {})", voidp{this}, cvoidp{dscBufferDesc},
        voidp{dsCaptureBuffer}, voidp{outer});

    if(!dsCaptureBuffer)
    {
        WARN(PREFIX "dsCaptureBuffer is null");
        return DSERR_INVALIDPARAM;
    }
    *dsCaptureBuffer = nullptr;

    if(outer)
    {
        WARN(PREFIX "Aggregation isn't supported");
        return DSERR_NOAGGREGATION;
    }
    if(!dscBufferDesc || dscBufferDesc->dwSize < sizeof(DSCBUFFERDESC1))
    {
        WARN(PREFIX "Invalid DSBUFFERDESC ({}, {})", cvoidp{dscBufferDesc},
             dscBufferDesc ? dscBufferDesc->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    auto bufdesc = DSCBUFFERDESC{};
    std::memcpy(&bufdesc, dscBufferDesc, std::min<DWORD>(sizeof(bufdesc), dscBufferDesc->dwSize));
    bufdesc.dwSize = std::min<DWORD>(sizeof(bufdesc), dscBufferDesc->dwSize);

    try {
        auto dscbuf = DSCBuffer::Create(mIs8);
        if(auto hr = dscbuf->Initialize(this, &bufdesc); FAILED(hr))
            return hr;

        *dsCaptureBuffer = dscbuf.release()->as<IDirectSoundCaptureBuffer*>();
    }
    catch(std::exception &e) {
        ERR(PREFIX "Exception creating buffer: {}", e.what());
        return E_FAIL;
    }

    return DS_OK;
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
