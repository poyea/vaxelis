// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// The scene-node handle, on its own so a header can name it without pulling in
/// every component definition.

#include <cstdint>

#include <entt/entt.hpp>

namespace vaxelis {

/// Handle for one scene node.
///
/// An alias for entt's entity while the scene is stored in an entt::registry.
/// The archetype port swaps it for `ecs::Entity`, so code that spells this name
/// instead of `entt::entity` needs no edit when that happens. Prefer it, and
/// prefer the typed accessors on Scene over reaching for registry().
using NodeId = entt::entity;

/// The handle that refers to no node. Use in place of `entt::null`.
inline constexpr NodeId kNoNode = static_cast<NodeId>(entt::null);

/// A node handle flattened to one integer, for the places that need a key
/// rather than a handle: ImGui widget ids, Lua, log lines.
///
/// 64 bits because the archetype handle is an index plus a generation, and
/// dropping the generation would make a recycled slot indistinguishable from
/// the entity that used to hold it. Opaque: pass it around, compare it, give it
/// back to node_from_key(), but do not do arithmetic on it.
using NodeKey = uint64_t;

/// Flattens `e`. Round-trips through node_from_key().
constexpr NodeKey node_key(NodeId e) {
    return static_cast<NodeKey>(static_cast<uint32_t>(e));
}

/// Rebuilds the handle `node_key()` flattened. A key from a different world, or
/// one that has been tampered with, yields a handle that reads as dead rather
/// than one that aliases a live node.
constexpr NodeId node_from_key(NodeKey key) {
    return static_cast<NodeId>(static_cast<uint32_t>(key));
}

} // namespace vaxelis
