#ifndef COMHELPERS_H
#define COMHELPERS_H

#include <utility>
#include <windows.h>


class PropVariant {
    PROPVARIANT mProp;

public:
    PropVariant() { PropVariantInit(&mProp); }
    PropVariant(const PropVariant&) = delete;
    ~PropVariant() { clear(); }

    PropVariant& operator=(const PropVariant&) = delete;

    void clear() { PropVariantClear(&mProp); }

    PROPVARIANT* get() noexcept { return &mProp; }

    PROPVARIANT& operator*() noexcept { return mProp; }
    const PROPVARIANT& operator*() const noexcept { return mProp; }

    PROPVARIANT* operator->() noexcept { return &mProp; }
    const PROPVARIANT* operator->() const noexcept { return &mProp; }
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
