#include "factory.h"

#include <array>

#include "capture.h"
#include "dsoundoal.h"
#include "fullduplex.h"
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

HRESULT CreateDSFullDuplex(REFIID riid, void **ppvObject)
{
    auto dsobj = DSFullDuplex::Create();
    return dsobj->QueryInterface(riid, ppvObject);
}

HRESULT CreateDSPrivatePropSet(REFIID riid, void **ppvObject)
{
    auto dsobj = DSPrivatePropertySet::Create();
    return dsobj->QueryInterface(riid, ppvObject);
}

std::array sFactories{
    Factory{CLSID_DirectSound8, CreateDS8},
    Factory{CLSID_DirectSound, CreateDS},
    Factory{CLSID_DirectSoundCapture8, CreateDSCapture8},
    Factory{CLSID_DirectSoundCapture, CreateDSCapture},
    Factory{CLSID_DirectSoundFullDuplex, CreateDSFullDuplex},
    Factory{CLSID_DirectSoundPrivate, CreateDSPrivatePropSet},
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

    FIXME(PREFIX "GetFactory No class found for {}", ClsidPrinter{clsid}.c_str());
    return CLASS_E_CLASSNOTAVAILABLE;
}


HRESULT STDMETHODCALLTYPE Factory::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG(PREFIX "QueryInterface ({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(),
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

    FIXME(PREFIX "QueryInterface Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE Factory::AddRef() noexcept
{
    const auto ret = mRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef ({}) ref {}", voidp{this}, ret);
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
            WARN(PREFIX "Release ({}) ref already {}", voidp{this}, ret);
            return ret;
        }
    } while(!mRef.compare_exchange_weak(ret, ret-1, std::memory_order_relaxed));
    ret -= 1;
    DEBUG(PREFIX "Release ({}) ref {}", voidp{this}, ret);
    return ret;
}

HRESULT STDMETHODCALLTYPE Factory::CreateInstance(IUnknown *unkOuter, REFIID riid, void **ppvObject) noexcept
{
    TRACE(PREFIX "CreateInstance ({})->({}, {}, {})", voidp{this}, voidp{unkOuter},
        IidPrinter{riid}.c_str(), voidp{ppvObject});

    if(!ppvObject)
    {
        WARN(PREFIX "CreateInstance NULL output parameter");
        return DSERR_INVALIDPARAM;
    }
    *ppvObject = nullptr;

    if(unkOuter)
        return CLASS_E_NOAGGREGATION;

    return mCreateInstance(riid, ppvObject);
}

HRESULT STDMETHODCALLTYPE Factory::LockServer(BOOL dolock) noexcept
{
    FIXME(PREFIX "LockServer ({})->({}): stub", voidp{this}, dolock);
    return S_OK;
}
