#ifndef EXPECTED_H
#define EXPECTED_H

#include <type_traits>
#include <utility>
#include <variant>

namespace ds {

#define REQUIRES(...) std::enable_if_t<(__VA_ARGS__), bool> = true

template<typename T>
struct remove_cvref {
    using type = std::remove_cv_t<std::remove_reference_t<T>>;
};
template<typename T>
using remove_cvref_t = typename remove_cvref<T>::type;


template<typename E>
class unexpected {
    E mError;

public:
    constexpr unexpected(const unexpected&) = default;
    constexpr unexpected(unexpected&&) = default;
    template<typename E2=E, REQUIRES(!std::is_same_v<remove_cvref_t<E2>, unexpected>
        && !std::is_same_v<remove_cvref_t<E2>, std::in_place_t>
        && std::is_constructible_v<E, E2>)>
    constexpr explicit unexpected(E2&& rhs) : mError{std::forward<E2>(rhs)}
    { }
    template<typename ...Args, REQUIRES(std::is_constructible_v<E, Args...>)>
    constexpr explicit unexpected(std::in_place_t, Args&& ...args)
        : mError{std::forward<Args>(args)...}
    { }
    template<typename U, typename ...Args,
        REQUIRES(std::is_constructible_v<E, std::initializer_list<U>&, Args...>)>
    constexpr explicit unexpected(std::in_place_t, std::initializer_list<U> il, Args&& ...args)
        : mError{il, std::forward<Args>(args)...}
    { }

    constexpr const E& error() const& noexcept { return mError; }
    constexpr E& error() & noexcept { return mError; }
    constexpr const E&& error() const&& noexcept { return std::move(mError); }
    constexpr E&& error() && noexcept { return std::move(mError); }

    constexpr void swap(unexpected& other) noexcept(std::is_nothrow_swappable_v<E>)
    { std::swap(mError, other.mError); }

    template<typename E2>
    friend constexpr bool operator==(const unexpected& lhs, const unexpected<E2>& rhs)
    { return lhs.error() == rhs.error(); }

    friend constexpr void swap(unexpected& lhs, unexpected& rhs) noexcept(noexcept(lhs.swap(rhs)))
    { lhs.swap(rhs); }
};

template<typename E>
unexpected(E) -> unexpected<E>;


template<typename S, typename F>
class expected {
    using variant_type = std::variant<S,F>;

    std::variant<S,F> mValues;

public:
    constexpr expected() noexcept(std::is_nothrow_default_constructible_v<variant_type>) = default;
    constexpr expected(const expected &rhs) noexcept(std::is_nothrow_copy_constructible_v<variant_type>) = default;
    constexpr expected(expected&& rhs) noexcept(std::is_nothrow_move_constructible_v<variant_type>) = default;

    /* Value constructors */
    template<typename U=S, REQUIRES(!std::is_same_v<remove_cvref_t<U>, std::in_place_t>
        && !std::is_same_v<expected, remove_cvref_t<U>>
        && std::is_constructible_v<S, U>
        && !std::is_convertible_v<U, S>)>
    constexpr explicit expected(U&& v) : mValues{std::in_place_index_t<0>{}, std::forward<U>(v)}
    { }
    template<typename U=S, REQUIRES(!std::is_same_v<remove_cvref_t<U>, std::in_place_t>
        && !std::is_same_v<expected, remove_cvref_t<U>>
        && std::is_constructible_v<S, U>
        && std::is_convertible_v<U, S>)>
    constexpr expected(U&& v) : mValues{std::in_place_index_t<0>{}, std::forward<U>(v)}
    { }

    /* Error constructors */
    template<typename T, REQUIRES(std::is_constructible_v<F, const T&>
        && !std::is_convertible_v<const T&, F>)>
    constexpr explicit expected(const unexpected<T> &rhs)
        : mValues{variant_type{std::in_place_index<1>, rhs.error()}}
    { }
    template<typename T, REQUIRES(std::is_constructible_v<F, const T&>
        && std::is_convertible_v<const T&, F>)>
    constexpr expected(const unexpected<T> &rhs)
        : mValues{variant_type{std::in_place_index<1>, rhs.error()}}
    { }

    template<typename T, REQUIRES(std::is_constructible_v<F, T>
        && !std::is_convertible_v<T, F>)>
    constexpr explicit expected(unexpected<T>&& rhs)
        : mValues{variant_type{std::in_place_index<1>, std::move(rhs.error())}}
    { }
    template<typename T, REQUIRES(std::is_constructible_v<F, T>
        && std::is_convertible_v<T, F>)>
    constexpr expected(unexpected<T>&& rhs)
        : mValues{variant_type{std::in_place_index<1>, std::move(rhs.error())}}
    { }

    constexpr bool has_value() const noexcept { return mValues.index() == 0; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr S& value() & { return std::get<0>(mValues); }
    constexpr const S& value() const& { return std::get<0>(mValues); }
    constexpr S&& value() && { return std::move(std::get<0>(mValues)); }
    constexpr const S&& value() const&& { return std::move(std::get<0>(mValues)); }

    constexpr S* operator->() noexcept { return &std::get<0>(mValues); }
    constexpr const S* operator->() const noexcept { return &std::get<0>(mValues); }
    constexpr S& operator*() noexcept { return std::get<0>(mValues); }
    constexpr const S& operator*() const noexcept { return std::get<0>(mValues); }

    template<typename U>
    constexpr S value_or(U&& defval) const&
    { return bool(*this) ? **this : static_cast<S>(std::forward<U>(defval)); }
    template<typename U>
    constexpr S value_or(U&& defval) &&
    { return bool(*this) ? std::move(**this) : static_cast<S>(std::forward<U>(defval)); }

    constexpr F& error() & { return std::get<1>(mValues); }
    constexpr const F& error() const& { return std::get<1>(mValues); }
    constexpr F&& error() && { return std::move(std::get<1>(mValues)); }
    constexpr const F&& error() const&& { return std::move(std::get<1>(mValues)); }
};

#undef REQUIRES

} // namespace ds

#endif // EXPECTED_H
