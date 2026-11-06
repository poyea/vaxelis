// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "engine/core/Uuid.hpp"
#include "engine/math/Math.hpp"
#include "engine/rhi/Rhi.hpp"

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


/// Stable identity. Assigned once at node creation and preserved across
/// save/load, so references survive reordering and stay unique across merges.
/// (entt handles, by contrast, are reused and per-registry.)
struct Id {
    Uuid uuid;
};

/// Display name. Default-constructed entities get "Node" so the inspector tree
/// never shows a blank row.
struct Name {
    std::string value{"Node"};
};

/// Scene-graph link. Roots have `parent == kNoNode`. Children list is kept
/// in insertion order; reordering is a UX concern, not stored here.
struct Hierarchy {
    NodeId parent{kNoNode};
    std::vector<NodeId> children;
};

/// 2D transform. Local space relative to parent.
struct Transform2D {
    vec2 position{0.0f, 0.0f};
    float rotation{0.0f}; ///< radians
    vec2 scale{1.0f, 1.0f};

    /// Composes T * R * S into a 4x4 matrix (2D in the XY plane).
    mat4 local_matrix() const {
        // T * R * S, 2D in XY plane.
        const float c = std::cos(rotation);
        const float s = std::sin(rotation);
        mat4 m(1.0f);
        m[0] = vec4(c * scale.x, s * scale.x, 0.0f, 0.0f);
        m[1] = vec4(-s * scale.y, c * scale.y, 0.0f, 0.0f);
        m[2] = vec4(0.0f, 0.0f, 1.0f, 0.0f);
        m[3] = vec4(position, 0.0f, 1.0f);
        return m;
    }
};

/// Cached world-space transform, written by Scene::update_world_transforms each
/// frame. Renderers read this instead of walking parents per-sprite.
struct WorldTransform2D {
    mat4 matrix{1.0f};
};

/// Renderable sprite. `texture_key` is a string ID the host resolves against
/// its asset table; keeps serialization texture-agnostic. `texture` is the
/// runtime-resolved handle, not serialized.
struct SpriteComponent {
    std::string texture_key;
    rhi::TextureHandle texture{};
    vec2 size{32.0f, 32.0f};
    vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};
    vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    int z_order{0};
    bool visible{true};
};

} // namespace vaxelis
