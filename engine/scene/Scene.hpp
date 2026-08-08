#pragma once

#include <functional>
#include <string>
#include <string_view>

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
    Scene();

    /// Direct registry access; see the class note about hierarchy invariants.
    entt::registry& registry() { return m_registry; }
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

    /// Single DFS from root that writes WorldTransform2D into every entity that
    /// has Transform2D + Hierarchy. O(N). Call once per frame before reading
    /// WorldTransform2D from any system that needs world space (rendering,
    /// picking, etc.). render_sprites calls this implicitly.
    void update_world_transforms();

    /// Depth-first traversal in child order. Visitor is called for each entity
    /// including the root.
    void for_each(const std::function<void(entt::entity)>& visit) const;

    /// Render every visible sprite with its world transform applied. Sprites
    /// with !texture.valid() are skipped (texture resolution is the host's job).
    void render_sprites(SpriteBatch& batch) const;

  private:
    void destroy_recursive(entt::entity e);
    void detach_from_parent(entt::entity e);

    entt::registry m_registry;
    entt::entity m_root{entt::null};
};

} // namespace vaxelis
