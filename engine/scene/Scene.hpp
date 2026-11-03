// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "engine/math/Math.hpp"
#include "engine/scene/Components.hpp"

namespace vaxelis {

class SpriteBatch;

/// Scene = entt::registry + an implicit root entity that holds the top-level
/// nodes. Hierarchy edits go through the helpers below so parent/child stay in
/// sync; touching the registry directly skips those invariants.
class Scene {
  public:
    /// Creates an empty scene holding just the root entity.
    Scene();

    /// Direct registry access; see the class note about hierarchy invariants.
    entt::registry& registry() { return m_registry; }
    /// Read-only registry access, for systems that only query.
    const entt::registry& registry() const { return m_registry; }
    /// The implicit root entity that parents all top-level nodes.
    entt::entity root() const { return m_root; }

    /// Creates a node with an Id + Name + Hierarchy + Transform2D. Parent
    /// defaults to root. A fresh Uuid is generated unless `uuid` is valid, in
    /// which case it is adopted (used by deserialization to restore identity).
    /// Caller adds extra components via registry().emplace<>.
    entt::entity create_node(std::string name, entt::entity parent = entt::null, Uuid uuid = {});

    /// Finds the node carrying `uuid`, or entt::null. O(N) scan, intended for
    /// load/merge wiring, not per-frame use.
    entt::entity find_by_uuid(const Uuid& uuid) const;

    /// Destroys `e` and all descendants. Removes the entry from the parent's
    /// child list. No-op if `e` is null or already destroyed.
    void destroy_node(entt::entity e);

    /// Reparents `e` under `new_parent` (or root if null). Cycles are rejected.
    void set_parent(entt::entity e, entt::entity new_parent);

    /// One-shot world transform; walks parents to the root. O(depth). Prefer
    /// the cached WorldTransform2D component refreshed by update_world_transforms
    /// for the common per-frame case.
    mat4 world_matrix(entt::entity e) const;

    /// Writes WorldTransform2D into every node reachable from the root. O(N),
    /// a single forward pass over a cached depth-ordered node list rather than
    /// a tree walk. Call once per frame before reading WorldTransform2D from
    /// any system that needs world space (rendering, picking, etc.).
    /// render_sprites calls this implicitly.
    void update_world_transforms();

    /// Depth-first traversal in child order. Visitor is called for each entity
    /// including the root.
    void for_each(const std::function<void(entt::entity)>& visit) const;

    /// Render every visible sprite with its world transform applied. Sprites
    /// with !texture.valid() are skipped (texture resolution is the host's job).
    void render_sprites(SpriteBatch& batch) const;

  private:
    static constexpr size_t kNoParent = static_cast<size_t>(-1);

    /// One node in the flattened traversal order, paired with the index of its
    /// parent in the same list. The order is DFS preorder, so a parent always
    /// precedes its children and its world matrix is ready by the time the
    /// child is reached.
    struct OrderEntry {
        entt::entity entity{entt::null};
        size_t parent{kNoParent};
    };

    /// One sprite awaiting its draw call, carrying the keys it is sorted by.
    struct SpriteDrawItem {
        entt::entity entity{entt::null};
        int z_order{0};
        uint32_t texture{0};
    };

    void destroy_recursive(entt::entity e);
    void detach_from_parent(entt::entity e);

    /// Recomputes m_order from the hierarchy. Called lazily from
    /// update_world_transforms when a structural edit has invalidated it.
    void rebuild_order();

    entt::registry m_registry;
    entt::entity m_root{entt::null};

    std::vector<OrderEntry> m_order;
    /// World matrix per m_order slot, so a child reads its parent's result by
    /// index instead of looking the component back up.
    std::vector<mat4> m_world;
    bool m_order_dirty{true};

    /// Sort scratch for render_sprites, kept between frames so a steady scene
    /// stops hitting the heap once the vector has grown to its working size.
    /// Mutable because rendering is logically a read of the scene.
    mutable std::vector<SpriteDrawItem> m_sprite_items;
};

} // namespace vaxelis
