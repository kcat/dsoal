#ifndef COMPTR_H
#define COMPTR_H

#include <type_traits>
#include <utility>
#include <variant>

template<typename T>
struct ComPtr {
    using element_type = T;

    ComPtr() noexcept = default;
    ComPtr(const ComPtr &rhs) noexcept(noexcept(mPtr->AddRef())) : mPtr{rhs.mPtr}
    { if(mPtr) mPtr->AddRef(); }
    ComPtr(ComPtr&& rhs) noexcept : mPtr{rhs.mPtr} { rhs.mPtr = nullptr; }
    ComPtr(std::nullptr_t) noexcept { } /* NOLINT(google-explicit-constructor) */
    explicit ComPtr(T *ptr) noexcept : mPtr{ptr} { }
    ~ComPtr() { if(mPtr) mPtr->Release(); }

    /* NOLINTNEXTLINE(bugprone-unhandled-self-assignment) Yes it is. */
    ComPtr& operator=(const ComPtr &rhs)
        noexcept(noexcept(rhs.mPtr->AddRef()) && noexcept(mPtr->Release()))
    {
        if constexpr(noexcept(rhs.mPtr->AddRef()) && noexcept(mPtr->Release()))
        {
            if(rhs.mPtr) rhs.mPtr->AddRef();
            if(mPtr) mPtr->Release();
            mPtr = rhs.mPtr;
            return *this;
        }
        else
        {
            ComPtr tmp{rhs};
            if(mPtr) mPtr->Release();
            mPtr = tmp.release();
            return *this;
        }
    }
    ComPtr& operator=(ComPtr&& rhs) noexcept(noexcept(mPtr->Release()))
    {
        if(&rhs != this)
        {
            if(mPtr) mPtr->Release();
            mPtr = std::exchange(rhs.mPtr, nullptr);
        }
        return *this;
    }

    void reset(T *ptr=nullptr) noexcept(noexcept(mPtr->Release()))
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
    explicit out_ptr_t(SP &res) : mRes{res} { }
    ~out_ptr_t()
    {
        auto set_res = [this](auto &ptr)
        { mRes.reset(static_cast<PT>(ptr)); };
        std::visit(set_res, mPtr);
    }
    out_ptr_t(const out_ptr_t&) = delete;

    out_ptr_t& operator=(const out_ptr_t&) = delete;

    operator PT*() && noexcept /* NOLINT(google-explicit-constructor) */
    { return &std::get<PT>(mPtr); }

    operator void**() && noexcept /* NOLINT(google-explicit-constructor) */
    { return &mPtr.template emplace<void*>(); }
};

template<typename T=void, typename SP, typename ...Args>
auto out_ptr(SP &res)
{
    using ptype = typename SP::element_type*;
    return out_ptr_t<SP,ptype>{res};
}

} // namespace ds

#endif // COMPTR_H
