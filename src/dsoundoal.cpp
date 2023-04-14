#include "dsoundoal.h"

#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

} // namespace

ComPtr<DSound8OAL> DSound8OAL::Create(bool is8)
{
    return ComPtr<DSound8OAL>{new DSound8OAL{is8}};
}

DSound8OAL::DSound8OAL(bool is8) : mIs8{is8} { };

DSound8OAL::~DSound8OAL() = default;


HRESULT STDMETHODCALLTYPE DSound8OAL::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG("DSound8OAL::QueryInterface (%p)->(%s, %p)\n", voidp{this}, GuidPrinter{riid}.c_str(),
        voidp{ppvObject});

    *ppvObject = NULL;
    if(riid == IID_IUnknown)
    {
        AddRef();
        *ppvObject = static_cast<IUnknown*>(as<IDirectSound8*>());
        return S_OK;
    }
    if(riid == IID_IDirectSound8)
    {
        if(!mIs8) UNLIKELY
        {
            WARN("DSound8OAL::QueryInterface Requesting IDirectSound8 iface for non-DS8 object\n");
            return E_NOINTERFACE;
        }
        AddRef();
        *ppvObject = as<IDirectSound8*>();
        return S_OK;
    }
    if(riid == IID_IDirectSound)
    {
        AddRef();
        *ppvObject = as<IDirectSound*>();
        return S_OK;
    }

    FIXME("Unhandled GUID: %s\n", GuidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE DSound8OAL::AddRef() noexcept
{
    const auto ret = mRef.fetch_add(std::memory_order_relaxed) + 1;
    DEBUG("DSound8OAL::AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE DSound8OAL::Release() noexcept
{
    const auto ret = mRef.fetch_sub(std::memory_order_relaxed) - 1;
    DEBUG("DSound8OAL::Release (%p) ref %lu\n", voidp{this}, ret);
    if(ret == 0) UNLIKELY
        delete this;
    return ret;
}


HRESULT STDMETHODCALLTYPE DSound8OAL::CreateSoundBuffer(const DSBUFFERDESC *bufferDesc,
    IDirectSoundBuffer **dsBuffer, IUnknown *outer) noexcept
{
    DEBUG("DSound8OAL::CreateSoundBuffer (%p)->(%p, %p, %p)\n", voidp{this}, cvoidp{bufferDesc},
        voidp{dsBuffer}, voidp{outer});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::GetCaps(DSCAPS *dsCaps) noexcept
{
    DEBUG("DSound8OAL::GetCaps (%p)->(%p)\n", voidp{this}, voidp{dsCaps});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::DuplicateSoundBuffer(IDirectSoundBuffer *origBuffer,
    IDirectSoundBuffer **dupBuffer) noexcept
{
    DEBUG("DSound8OAL::DuplicateSoundBuffer (%p)->(%p, %p)\n", voidp{this}, voidp{origBuffer},
        voidp{dupBuffer});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::SetCooperativeLevel(HWND hwnd, DWORD level) noexcept
{
    DEBUG("DSound8OAL::SetCooperativeLevel (%p)->(%p, %lu)\n", voidp{this}, voidp{hwnd}, level);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::Compact() noexcept
{
    DEBUG("DSound8OAL::DuplicateSoundBuffer (%p)->()\n", voidp{this});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::GetSpeakerConfig(DWORD *speakerConfig) noexcept
{
    DEBUG("DSound8OAL::GetSpeakerConfig (%p)->(%p)\n", voidp{this}, voidp{speakerConfig});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::SetSpeakerConfig(DWORD speakerConfig) noexcept
{
    DEBUG("DSound8OAL::SetSpeakerConfig (%p)->(%lx)\n", voidp{this}, speakerConfig);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::Initialize(const GUID *deviceId) noexcept
{
    DEBUG("DSound8OAL::Initialize (%p)->(%s)\n", voidp{this}, GuidPrinter{deviceId}.c_str());
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE DSound8OAL::VerifyCertification(DWORD *certified) noexcept
{
    DEBUG("DSound8OAL::VerifyCertification (%p)->(%p)\n", voidp{this}, voidp{certified});
    return E_NOTIMPL;
}
