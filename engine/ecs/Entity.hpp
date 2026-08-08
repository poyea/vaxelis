#pragma once

/// @file
/// Entity handles for the archetype world.

#include <cstdint>

namespace vaxelis::ecs {

/// Handle to an entity: a slot index plus the generation that slot carried when
/// the handle was made.
///
/// Slots are recycled, so an index on its own would silently alias whatever
/// entity reused it. Destroying an entity bumps its slot's generation, which
/// makes every handle still pointing at the old entity compare unequal and read
/// as dead instead of quietly addressing a stranger.
struct Entity {
    uint32_t index{0};
    /// Zero is never handed out, so a default-constructed handle is never alive.
    uint32_t generation{0};

    constexpr bool operator==(const Entity&) const = default;
};

/// The handle no live entity ever has.
inline constexpr Entity kNoEntity{};

} // namespace vaxelis::ecs
