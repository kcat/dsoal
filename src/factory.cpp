#include "factory.h"

#include "capture.h"
#include "dsoundoal.h"
#include "guidprinter.h"
#include "logging.h"
#include "propset.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

HRESULT CreateDS8(REFIID riid, void **ppvObject)
{
    auto dsobj = DSound8OAL::Create(true);
    return dsobj->QueryInterface(riid, ppvObject);
}
HRESULT CreateDS(REFIID riid, void **ppvObject)
{
    auto dsobj = DSound8OAL::Create(false);
    return dsobj->QueryInterface(riid, ppvObject);
}

HRESULT CreateDSCapture8(REFIID riid, void **ppvObject)
{
    auto dsobj = DSCapture::Create(true);
    return dsobj->QueryInterface(riid, ppvObject);
}
HRESULT CreateDSCapture(REFIID riid, void **ppvObject)
{
    auto dsobj = DSCapture::Create(false);
    return dsobj->QueryInterface(riid, ppvObject);
}

HRESULT CreateDSPrivatePropSet(REFIID riid, void **ppvObject)
{
    auto dsobj = DSPrivatePropertySet::Create();
    return dsobj->QueryInterface(riid, ppvObject);
}

Factory sFactories[]{
    {CLSID_DirectSound8, CreateDS8},
    {CLSID_DirectSound, CreateDS},
    {CLSID_DirectSoundCapture8, CreateDSCapture8},
    {CLSID_DirectSoundCapture, CreateDSCapture},
    {CLSID_DirectSoundPrivate, CreateDSPrivatePropSet},
};

} // namespace


#define PREFIX "Factory::"
HRESULT Factory::GetFactory(const GUID &clsid, const GUID &iid, void **out)
{
    for(auto &factory : sFactories)
    {
        if(clsid == factory.getId())
            return factory.QueryInterface(iid, out);
    }

    FIXME(PREFIX "GetFactory No class found for %s\n", ClsidPrinter{clsid}.c_str());
    return CLASS_E_CLASSNOTAVAILABLE;
}


HRESULT STDMETHODCALLTYPE Factory::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG(PREFIX "QueryInterface (%p)->(%s, %p)\n", voidp{this}, IidPrinter{riid}.c_str(),
        voidp{ppvObject});

    if(!ppvObject)
        return E_POINTER;
    *ppvObject = nullptr;

    if(riid == IID_IUnknown)
    {
        AddRef();
        *ppvObject = as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IClassFactory)
    {
        AddRef();
        *ppvObject = as<IClassFactory*>();
        return S_OK;
    }

    FIXME(PREFIX "QueryInterface Unhandled GUID: %s\n", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE Factory::AddRef() noexcept
{
    const auto ret = mRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Factory::Release() noexcept
{
    /* The factory is a static object and should not be deleted. Make sure the
     * reference count doesn't underflow.
     */
    ULONG ret{mRef.load(std::memory_order_relaxed)};
    do {
        if(ret == 0) UNLIKELY
        {
            WARN(PREFIX "Release (%p) ref already %lu\n", voidp{this}, ret);
            return ret;
        }
    } while(!mRef.compare_exchange_weak(ret, ret-1, std::memory_order_relaxed));
    ret -= 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

HRESULT STDMETHODCALLTYPE Factory::CreateInstance(IUnknown *unkOuter, REFIID riid, void **ppvObject) noexcept
{
    TRACE(PREFIX "CreateInstance (%p)->(%p, %s, %p)\n", voidp{this}, voidp{unkOuter},
        IidPrinter{riid}.c_str(), voidp{ppvObject});

    if(!ppvObject)
    {
        WARN(PREFIX "CreateInstance NULL output parameter\n");
        return DSERR_INVALIDPARAM;
    }
    *ppvObject = nullptr;

    if(unkOuter)
        return CLASS_E_NOAGGREGATION;

    return mCreateInstance(riid, ppvObject);
}

HRESULT STDMETHODCALLTYPE Factory::LockServer(BOOL dolock) noexcept
{
    FIXME(PREFIX "LockServer (%p)->(%d): stub\n", voidp{this}, dolock);
    return S_OK;
}
