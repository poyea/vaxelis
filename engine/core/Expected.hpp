#pragma once

// Drop-in for std::expected. Uses the standard one when the library exposes
// it (libstdc++ 12+, libc++ 17+, MSVC STL 19.34+); otherwise falls back to a
// minimal variant-backed shim so the engine builds on toolchains where the
// C++23 header is present but the symbol isn't (e.g. some clang-18 + older
// libstdc++ pairings).

#include <version>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#  include <expected>
namespace vaxelis {
template <class T, class E> using expected   = std::expected<T, E>;
template <class E>          using unexpected = std::unexpected<E>;
}  // namespace vaxelis
#else
#  include <utility>
#  include <variant>

namespace vaxelis {

template <class E>
class unexpected {
public:
    constexpr explicit unexpected(const E& e) : err_(e) {}
    constexpr explicit unexpected(E&& e) : err_(std::move(e)) {}
    constexpr const E&  error() const&  noexcept { return err_; }
    constexpr E&        error() &       noexcept { return err_; }
    constexpr E&&       error() &&      noexcept { return std::move(err_); }

private:
    E err_;
};

template <class T, class E>
class expected {
public:
    constexpr expected(const T& v) : data_(std::in_place_index<0>, v) {}
    constexpr expected(T&& v) : data_(std::in_place_index<0>, std::move(v)) {}
    constexpr expected(const unexpected<E>& u) : data_(std::in_place_index<1>, u.error()) {}
    constexpr expected(unexpected<E>&& u)
        : data_(std::in_place_index<1>, std::move(u).error()) {}

    constexpr bool          has_value() const noexcept { return data_.index() == 0; }
    constexpr explicit      operator bool() const noexcept { return has_value(); }

    constexpr const T&      operator*() const& { return std::get<0>(data_); }
    constexpr T&            operator*() & { return std::get<0>(data_); }
    constexpr T&&           operator*() && { return std::move(std::get<0>(data_)); }
    constexpr const T*      operator->() const { return &std::get<0>(data_); }
    constexpr T*            operator->() { return &std::get<0>(data_); }

    constexpr const E&      error() const& { return std::get<1>(data_); }
    constexpr E&            error() & { return std::get<1>(data_); }
    constexpr E&&           error() && { return std::move(std::get<1>(data_)); }

    template <class U>
    constexpr T value_or(U&& def) const& {
        return has_value() ? std::get<0>(data_) : T(std::forward<U>(def));
    }

private:
    // Index 0 = value, 1 = error. Requires T and E to be distinct types,
    // which holds for every use site in this engine.
    std::variant<T, E> data_;
};

}  // namespace vaxelis
#endif
