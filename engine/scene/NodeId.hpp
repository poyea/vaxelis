// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// The scene-node handle, on its own so a header can name it without pulling in
/// every component definition.

#include <cstdint>

#include "engine/ecs/Entity.hpp"

namespace vaxelis {

/// Handle for one scene node: an entity in the scene's archetype world.
///
/// An index plus the generation its slot carried when the handle was made, so a
/// handle to a destroyed node reads as dead rather than aliasing whatever node
/// reused the slot. Not an integer: go through node_key() for a key.
using NodeId = ecs::Entity;

/// The handle that refers to no node.
inline constexpr NodeId kNoNode = ecs::kNoEntity;

/// A node handle flattened to one integer, for the places that need a key
/// rather than a handle: ImGui widget ids, Lua, log lines.
///
/// 64 bits because the handle is an index plus a generation, and dropping the
/// generation would make a recycled slot indistinguishable from the node that
/// used to hold it. Opaque: pass it around, compare it, give it back to
/// node_from_key(), but do not do arithmetic on it.
using NodeKey = uint64_t;

/// Flattens `e`. Round-trips through node_from_key().
constexpr NodeKey node_key(NodeId e) {
    return (static_cast<NodeKey>(e.generation) << 32) | static_cast<NodeKey>(e.index);
}

/// Rebuilds the handle `node_key()` flattened. A key from a different world, or
/// one that has been tampered with, yields a handle that reads as dead rather
/// than one that aliases a live node.
constexpr NodeId node_from_key(NodeKey key) {
    return NodeId{.index = static_cast<uint32_t>(key & 0xffffffffU),
                  .generation = static_cast<uint32_t>(key >> 32)};
}

} // namespace vaxelis
