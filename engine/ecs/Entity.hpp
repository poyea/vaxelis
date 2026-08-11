// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// Entity handles for the archetype world.

#include <cstdint>

namespace vaxelis::ecs {

/// Handle to an entity: a slot index plus the generation that slot carried when
/// the handle was made.
///
/// Slots are recycled, so a bare index would silently alias whatever entity
/// reused it. Destroying an entity bumps its slot's generation, so stale
/// handles compare unequal and read as dead.
struct Entity {
    /// Slot this entity occupies in the world's record table.
    uint32_t index{0};
    /// Zero is never handed out, so a default-constructed handle is never alive.
    uint32_t generation{0};

    /// Handles match only when they name the same incarnation of a slot.
    constexpr bool operator==(const Entity&) const = default;
};

/// The handle no live entity ever has.
inline constexpr Entity kNoEntity{};

} // namespace vaxelis::ecs
