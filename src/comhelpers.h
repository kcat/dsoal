#ifndef COMHELPERS_H
#define COMHELPERS_H

#include <cassert>
#include <string>
#include <string_view>
#include <utility>
#include <windows.h>


class PropVariant {
    PROPVARIANT mProp{};

public:
    PropVariant() { PropVariantInit(&mProp); }
    PropVariant(const PropVariant &rhs) : PropVariant{} { PropVariantCopy(&mProp, &rhs.mProp); }
    ~PropVariant() { clear(); }

    auto operator=(const PropVariant &rhs) -> PropVariant&
    {
        if(this != &rhs)
            PropVariantCopy(&mProp, &rhs.mProp);
        return *this;
    }

    void clear() { PropVariantClear(&mProp); }

    auto get() noexcept -> PROPVARIANT* { return &mProp; }

    /* NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) */
    [[nodiscard]]
    auto type() const noexcept -> VARTYPE { return mProp.vt; }

    template<typename T> [[nodiscard]]
    auto value() const -> T
    {
        if constexpr(std::is_same_v<T,UINT>)
        {
            assert(mProp.vt == VT_UI4 || mProp.vt == VT_UINT);
            return mProp.uintVal;
        }
        else if constexpr(std::is_same_v<T,ULONG>)
        {
            assert(mProp.vt == VT_UI4 || mProp.vt == VT_UINT);
            return mProp.ulVal;
        }
        else if constexpr(std::is_same_v<T,std::wstring_view> || std::is_same_v<T,std::wstring>
            || std::is_same_v<T,LPWSTR> || std::is_same_v<T,LPCWSTR>)
        {
            assert(mProp.vt == VT_LPWSTR);
            return mProp.pwszVal;
        }
    }
    /* NOLINTEND(cppcoreguidelines-pro-type-union-access) */
};

struct ComWrapper {
    HRESULT mStatus{};

    ComWrapper() : mStatus{CoInitialize(nullptr)} { }
    ComWrapper(ComWrapper&& rhs) : mStatus{std::exchange(rhs.mStatus, E_FAIL)} { }
    ComWrapper(const ComWrapper&) = delete;
    ~ComWrapper() { if(SUCCEEDED(mStatus)) CoUninitialize(); }

    ComWrapper& operator=(ComWrapper&& rhs)
    {
        if(SUCCEEDED(mStatus))
            CoUninitialize();
        mStatus = std::exchange(rhs.mStatus, E_FAIL);
        return *this;
    }
    ComWrapper& operator=(const ComWrapper&) = delete;

    void uninit()
    {
        if(SUCCEEDED(mStatus))
            CoUninitialize();
        mStatus = E_FAIL;
    }
};

#endif // COMHELPERS_H
