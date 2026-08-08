#pragma once

/// @file
/// The set of component types an entity carries, as a bitmask.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "engine/ecs/ComponentType.hpp"

namespace vaxelis::ecs {

/// One bit per ComponentId. Two entities with the same signature live in the
/// same archetype, and a query is just a signature that every candidate
/// archetype must contain, so both tests are single integer operations.
class Signature {
  public:
    constexpr Signature() = default;
    constexpr explicit Signature(uint64_t bits) : m_bits(bits) {}

    /// Adds `id`. Ids at or past kMaxComponents are ignored rather than
    /// shifting out of range; registration already logged that failure.
    constexpr Signature& set(ComponentId id) {
        m_bits |= bit(id);
        return *this;
    }

    constexpr Signature& reset(ComponentId id) {
        m_bits &= ~bit(id);
        return *this;
    }

    constexpr bool test(ComponentId id) const { return (m_bits & bit(id)) != 0; }

    /// True when every component in `subset` is also here. This is the test a
    /// query runs against each archetype.
    constexpr bool contains(Signature subset) const {
        return (m_bits & subset.m_bits) == subset.m_bits;
    }

    /// Number of component types in the set.
    constexpr uint32_t count() const { return static_cast<uint32_t>(std::popcount(m_bits)); }

    constexpr bool empty() const { return m_bits == 0; }
    constexpr uint64_t bits() const { return m_bits; }

    constexpr bool operator==(const Signature&) const = default;

  private:
    static constexpr uint64_t bit(ComponentId id) {
        return id < kMaxComponents ? (uint64_t{1} << id) : uint64_t{0};
    }

    uint64_t m_bits{0};
};

/// The signature naming exactly `Ts`, registering any id not yet assigned.
template <class... Ts>
Signature signature_of() {
    Signature s;
    (s.set(component_id<Ts>()), ...);
    return s;
}

} // namespace vaxelis::ecs

/// Lets a signature key an unordered_map of archetypes.
template <>
struct std::hash<vaxelis::ecs::Signature> {
    std::size_t operator()(const vaxelis::ecs::Signature& s) const noexcept {
        return std::hash<uint64_t>{}(s.bits());
    }
};
