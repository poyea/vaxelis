// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace vaxelis {

/// 128-bit identifier, stable across saves/loads and unique across scene merges.
/// Default-constructed (all-zero) is the "null" id. Serialized as the canonical
/// 8-4-4-4-12 lowercase-hex form.
struct Uuid {
    uint64_t hi{0};
    uint64_t lo{0};

    /// True unless this is the all-zero null id.
    constexpr bool valid() const { return hi != 0 || lo != 0; }
    bool operator==(const Uuid&) const = default;
};

/// Random (version-4) UUID. Thread-safe.
Uuid generate_uuid();

/// Canonical "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx". Null id renders all-zero.
std::string to_string(const Uuid&);

/// Parses the canonical form (dashes optional). Returns a null Uuid on any
/// malformed input.
Uuid uuid_from_string(std::string_view);

} // namespace vaxelis

// Hash support so Uuid can key unordered containers.
/// @cond INTERNAL Doxygen mis-scopes specializations into namespace std.
template <> struct std::hash<vaxelis::Uuid> {
    std::size_t operator()(const vaxelis::Uuid& u) const noexcept {
        // Mix the halves; 64-bit hash is plenty for in-memory lookup.
        return std::hash<uint64_t>{}(u.hi) ^ (std::hash<uint64_t>{}(u.lo) << 1);
    }
};
/// @endcond
