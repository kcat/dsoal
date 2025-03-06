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

template<typename T, auto ...Params>
auto CreateObj(REFIID riid, void **ppvObject) -> HRESULT
{
    return T::Create(Params...)->QueryInterface(riid, ppvObject);
}

std::array sFactories{
    Factory{CLSID_DirectSound8, CreateObj<DSound8OAL, true>},
    Factory{CLSID_DirectSound, CreateObj<DSound8OAL, false>},
    Factory{CLSID_DirectSoundCapture8, CreateObj<DSCapture, true>},
    Factory{CLSID_DirectSoundCapture, CreateObj<DSCapture, false>},
    Factory{CLSID_DirectSoundFullDuplex, CreateObj<DSFullDuplex>},
    Factory{CLSID_DirectSoundPrivate, CreateObj<DSPrivatePropertySet>},
};

} // namespace


#define CLASS_PREFIX "Factory::"
#define PREFIX CLASS_PREFIX "GetFactory "
HRESULT Factory::GetFactory(const GUID &clsid, const GUID &iid, void **out)
{
    for(auto &factory : sFactories)
    {
        if(clsid == factory.getId())
            return factory.QueryInterface(iid, out);
    }

    FIXME("No class found for {}", ClsidPrinter{clsid}.c_str());
    return CLASS_E_CLASSNOTAVAILABLE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE Factory::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

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

    FIXME("Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE Factory::AddRef() noexcept
{
    const auto ret = mRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE Factory::Release() noexcept
{
    /* The factory is a static object and should not be deleted. Make sure the
     * reference count doesn't underflow.
     */
    ULONG ret{mRef.load(std::memory_order_relaxed)};
    do {
        if(ret == 0) [[unlikely]]
        {
            WARN("({}) ref already {}", voidp{this}, ret);
            return ret;
        }
    } while(!mRef.compare_exchange_weak(ret, ret-1, std::memory_order_relaxed));
    ret -= 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "CreateInstance "
HRESULT STDMETHODCALLTYPE Factory::CreateInstance(IUnknown *unkOuter, REFIID riid, void **ppvObject) noexcept
{
    TRACE("({})->({}, {}, {})", voidp{this}, voidp{unkOuter}, IidPrinter{riid}.c_str(),
        voidp{ppvObject});

    if(!ppvObject)
    {
        WARN("NULL output parameter");
        return DSERR_INVALIDPARAM;
    }
    *ppvObject = nullptr;

    if(unkOuter)
        return CLASS_E_NOAGGREGATION;

    return mCreateInstance(riid, ppvObject);
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "LockServer "
HRESULT STDMETHODCALLTYPE Factory::LockServer(BOOL dolock) noexcept
{
    FIXME("({})->({}): stub", voidp{this}, dolock);
    return S_OK;
}
#undef PREFIX
#undef CLASS_PREFIX
