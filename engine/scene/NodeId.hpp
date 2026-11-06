// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// The scene-node handle, on its own so a header can name it without pulling in
/// every component definition.

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

} // namespace vaxelis
