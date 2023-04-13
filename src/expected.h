#ifndef EXPECTED_H
#define EXPECTED_H

#include <utility>
#include <variant>

namespace ds {

template<typename S, typename F>
class expected : public std::variant<S,F> {
public:
    using std::variant<S,F>::variant;

    constexpr bool has_value() const noexcept { return this->index() == 0; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr S& value() & { return std::get<0>(*this); }
    constexpr const S& value() const& { return std::get<0>(*this); }
    constexpr S&& value() && { return std::move(std::get<0>(*this)); }
    constexpr const S&& value() const&& { return std::move(std::get<0>(*this)); }

    constexpr S* operator->() noexcept { return &std::get<0>(*this); }
    constexpr const S* operator->() const noexcept { return &std::get<0>(*this); }
    constexpr S& operator*() noexcept { return std::get<0>(*this); }
    constexpr const S& operator*() const noexcept { return std::get<0>(*this); }

    template<typename U>
    constexpr S value_or(U&& defval) const&
    { return bool(*this) ? **this : static_cast<S>(std::forward<U>(defval)); }
    template<typename U>
    constexpr S value_or(U&& defval) &&
    { return bool(*this) ? std::move(**this) : static_cast<S>(std::forward<U>(defval)); }

    constexpr F& error() & { return std::get<1>(*this); }
    constexpr const F& error() const& { return std::get<1>(*this); }
    constexpr F&& error() && { return std::move(std::get<1>(*this)); }
    constexpr const F&& error() const&& { return std::move(std::get<1>(*this)); }
};

} // namespace ds

#endif // EXPECTED_H
