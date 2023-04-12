#ifndef COMPTR_H
#define COMPTR_H

#include <cstddef>
#include <memory>
#include <utility>
#include <variant>

template<typename T>
struct ComPtr {
    using element_type = T;

    ComPtr() noexcept = default;
    ComPtr(const ComPtr &rhs) : mPtr{rhs.mPtr} { if(mPtr) mPtr->AddRef(); }
    ComPtr(ComPtr&& rhs) noexcept : mPtr{rhs.mPtr} { rhs.mPtr = nullptr; }
    ComPtr(std::nullptr_t) noexcept { }
    explicit ComPtr(T *ptr) noexcept : mPtr{ptr} { }
    ~ComPtr() { if(mPtr) mPtr->Release(); }

    ComPtr& operator=(const ComPtr &rhs)
    {
        if(!rhs.mPtr)
        {
            if(mPtr)
                mPtr->Release();
            mPtr = nullptr;
        }
        else
        {
            rhs.mPtr->AddRef();
            try {
                if(mPtr)
                    mPtr->Release();
                mPtr = rhs.mPtr;
            }
            catch(...) {
                rhs.mPtr->Release();
                throw;
            }
        }
        return *this;
    }
    ComPtr& operator=(ComPtr&& rhs)
    {
        if(&rhs != this)
        {
            if(mPtr) mPtr->Release();
            mPtr = std::exchange(rhs.mPtr, nullptr);
        }
        return *this;
    }

    void reset(T *ptr=nullptr)
    {
        if(mPtr) mPtr->Release();
        mPtr = ptr;
    }

    explicit operator bool() const noexcept { return mPtr != nullptr; }

    T& operator*() const noexcept { return *mPtr; }
    T* operator->() const noexcept { return mPtr; }
    T* get() const noexcept { return mPtr; }

    T* release() noexcept { return std::exchange(mPtr, nullptr); }

    void swap(ComPtr &rhs) noexcept { std::swap(mPtr, rhs.mPtr); }
    void swap(ComPtr&& rhs) noexcept { std::swap(mPtr, rhs.mPtr); }

private:
    T *mPtr{nullptr};
};


namespace ds {

template<typename SP, typename PT, typename ...Args>
class out_ptr_t {
    static_assert(!std::is_same_v<PT,void*>);

    SP &mRes;
    std::variant<PT,void*> mPtr{};

public:
    out_ptr_t(SP &res) : mRes{res} { }
    ~out_ptr_t()
    {
        auto set_res = [this](auto &ptr)
        { mRes.reset(static_cast<PT>(ptr)); };
        std::visit(set_res, mPtr);
    }
    out_ptr_t(const out_ptr_t&) = delete;

    out_ptr_t& operator=(const out_ptr_t&) = delete;

    operator PT*() noexcept
    { return &std::get<PT>(mPtr); }

    operator void**() noexcept
    {
        mPtr.template emplace<void*>();
        return &std::get<void*>(mPtr);
    }
};

template<typename T=void, typename SP, typename ...Args>
auto out_ptr(SP &res)
{
    using ptype = typename SP::element_type*;
    return out_ptr_t<SP,ptype>{res};
}

} // namespace ds

#endif // COMPTR_H
