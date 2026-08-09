// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// Drop-in for std::expected, including the monadic operations the engine
/// uses. Aliases the standard type when the library provides it; otherwise a
/// minimal variant-backed shim fills in.

#include <version>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
#include <expected>
namespace vaxelis {
template <class T, class E> using expected = std::expected<T, E>;
template <class E> using unexpected = std::unexpected<E>;
} // namespace vaxelis
#else
#include <type_traits>
#include <utility>
#include <variant>

namespace vaxelis {

/// Error wrapper for the fallback expected shim; mirrors std::unexpected.
template <class E> class unexpected {
  public:
    constexpr explicit unexpected(const E& e) : m_err(e) {}
    constexpr explicit unexpected(E&& e) : m_err(std::move(e)) {}
    constexpr const E& error() const& noexcept { return m_err; }
    constexpr E& error() & noexcept { return m_err; }
    constexpr E&& error() && noexcept { return std::move(m_err); }

  private:
    E m_err;
};

/// Minimal variant-backed std::expected shim; see the file comment.
template <class T, class E> class expected {
  public:
    constexpr expected(const T& v) : m_data(std::in_place_index<0>, v) {}
    constexpr expected(T&& v) : m_data(std::in_place_index<0>, std::move(v)) {}
    constexpr expected(const unexpected<E>& u) : m_data(std::in_place_index<1>, u.error()) {}
    constexpr expected(unexpected<E>&& u) : m_data(std::in_place_index<1>, std::move(u).error()) {}

    constexpr bool has_value() const noexcept { return m_data.index() == 0; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr const T& operator*() const& { return std::get<0>(m_data); }
    constexpr T& operator*() & { return std::get<0>(m_data); }
    constexpr T&& operator*() && { return std::move(std::get<0>(m_data)); }
    constexpr const T* operator->() const { return &std::get<0>(m_data); }
    constexpr T* operator->() { return &std::get<0>(m_data); }

    constexpr const E& error() const& { return std::get<1>(m_data); }
    constexpr E& error() & { return std::get<1>(m_data); }
    constexpr E&& error() && { return std::move(std::get<1>(m_data)); }

    template <class U> constexpr T value_or(U&& def) const& {
        return has_value() ? std::get<0>(m_data) : T(std::forward<U>(def));
    }

    /// Monadic operations: just the subset the engine uses. For and_then,
    /// `f` must return an expected with the same error type.
    template <class F> constexpr auto and_then(F&& f) const& {
        using R = std::remove_cvref_t<std::invoke_result_t<F, const T&>>;
        return has_value() ? std::forward<F>(f)(std::get<0>(m_data))
                           : R(unexpected<E>(std::get<1>(m_data)));
    }
    template <class F> constexpr auto and_then(F&& f) && {
        using R = std::remove_cvref_t<std::invoke_result_t<F, T&&>>;
        return has_value() ? std::forward<F>(f)(std::get<0>(std::move(m_data)))
                           : R(unexpected<E>(std::get<1>(std::move(m_data))));
    }
    template <class F> constexpr auto transform_error(F&& f) const& {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E&>>;
        return has_value() ? expected<T, G>(std::get<0>(m_data))
                           : expected<T, G>(unexpected<G>(std::forward<F>(f)(std::get<1>(m_data))));
    }
    template <class F> constexpr auto transform_error(F&& f) && {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E&&>>;
        return has_value() ? expected<T, G>(std::get<0>(std::move(m_data)))
                           : expected<T, G>(
                                 unexpected<G>(std::forward<F>(f)(std::get<1>(std::move(m_data)))));
    }

  private:
    // Index 0 = value, 1 = error. Requires T and E to be distinct types,
    // which holds for every use site in this engine.
    std::variant<T, E> m_data;
};

} // namespace vaxelis
#endif
