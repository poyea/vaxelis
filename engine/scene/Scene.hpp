// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/ecs/World.hpp"
#include "engine/math/Math.hpp"
#include "engine/scene/Components.hpp"

namespace vaxelis {

class SpriteBatch;

/// Scene = an archetype world plus an implicit root entity that holds the
/// top-level nodes. Hierarchy edits go through the helpers below so parent/child stay in
/// sync; touching the registry directly skips those invariants.
class Scene {
  public:
    /// Creates an empty scene holding just the root entity.
    Scene();

    /// Not copyable: ecs::World is move-only.
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    /// Movable, but only through these: the constructor installs Hierarchy
    /// construct/destroy listeners bound to `this`, and a world carries its
    /// subscriptions with it, so an implicit move would leave them naming the
    /// scene they came from.
    ///
    /// Both are implemented as swaps, so neither side is left hollow: the
    /// runtime clears a scene with `m_scene = Scene{}` and keeps using it. The
    /// constructor is not noexcept: it default-constructs a scene (one root
    /// node) before trading contents with the source.
    Scene(Scene&& other);
    Scene& operator=(Scene&& other) noexcept;

    /// Exchanges contents with `other`, rebinding both scenes' listeners.
    void swap(Scene& other) noexcept;

    /// Direct world access; see the class note about hierarchy invariants.
    /// Prefer the typed accessors below. This is for what they do not cover,
    /// such as subscribing to a component signal.
    ecs::World& world() { return m_world; }
    /// Read-only world access, for systems that only query. See world().
    const ecs::World& world() const { return m_world; }
    /// The implicit root entity that parents all top-level nodes.
    NodeId root() const { return m_root; }

    /// Creates a node with an Id + Name + Hierarchy + Transform2D. Parent
    /// defaults to root. A fresh Uuid is generated unless `uuid` is valid, in
    /// which case it is adopted (used by deserialization to restore identity).
    /// Caller adds extra components via registry().emplace<>.
    NodeId create_node(std::string name, NodeId parent = kNoNode, Uuid uuid = {});

    /// Finds the node carrying `uuid`, or kNoNode. O(N) scan, intended for
    /// load/merge wiring, not per-frame use.
    NodeId find_by_uuid(const Uuid& uuid) const;

    /// Destroys `e` and all descendants. Removes the entry from the parent's
    /// child list. No-op if `e` is null or already destroyed.
    void destroy_node(NodeId e);

    /// Reparents `e` under `new_parent` (or root if null). Cycles are rejected.
    void set_parent(NodeId e, NodeId new_parent);

    /// One-shot world transform; walks parents to the root. O(depth). Prefer
    /// the cached WorldTransform2D component refreshed by update_world_transforms
    /// for the common per-frame case.
    mat4 world_matrix(NodeId e) const;

    /// Writes WorldTransform2D into every node reachable from the root. O(N),
    /// a single forward pass over a cached depth-ordered node list rather than
    /// a tree walk. Call once per frame before reading WorldTransform2D from
    /// any system that needs world space (rendering, picking, etc.).
    /// render_sprites calls this implicitly.
    void update_world_transforms();

    // --- component access -------------------------------------------------
    // The typed face over the storage. Written so call sites name Scene rather
    // than the registry: the archetype port changes what is underneath these,
    // and anything that went through registry() directly has to be rewritten
    // by hand. Semantics match the storage calls they wrap.

    /// True while `e` refers to a live node.
    bool valid(NodeId e) const { return m_world.alive(e); }

    /// True when `e` carries a `T`.
    template <class T> bool has(NodeId e) const { return m_world.has<T>(e); }

    /// Gives `e` a `T`, built from `args`, replacing any it already had.
    ///
    /// A component the node does not carry yet migrates its row, so this is a
    /// structural change. Build a node through create_node rather than adding
    /// its components one at a time.
    ///
    /// Not for `Hierarchy`: parent and children are an invariant pair, and
    /// writing either one directly leaves the tree and the cached traversal
    /// order inconsistent -- a cycle written this way hangs set_parent() and
    /// overflows world_matrix(). Use create_node, destroy_node and set_parent.
    template <class T, class... Args> T& add(NodeId e, Args&&... args) {
        T* slot = m_world.add<T>(e, T{std::forward<Args>(args)...});
        assert(slot != nullptr && "Scene::add on a dead node");
        return *slot;
    }

    /// `e`'s `T`. Asserts in the storage if it has none; use try_get() when
    /// that is in question.
    template <class T> T& get(NodeId e) {
        T* slot = m_world.try_get<T>(e);
        assert(slot != nullptr && "Scene::get on a node without that component");
        return *slot;
    }
    /// Read-only overload of get().
    template <class T> const T& get(NodeId e) const {
        const T* slot = m_world.try_get<T>(e);
        assert(slot != nullptr && "Scene::get on a node without that component");
        return *slot;
    }

    /// `e`'s `T`, or nullptr when it has none or the handle is stale.
    template <class T> T* try_get(NodeId e) { return m_world.try_get<T>(e); }
    /// Read-only overload of try_get().
    template <class T> const T* try_get(NodeId e) const { return m_world.try_get<T>(e); }

    /// Takes `T` away from `e`. A no-op when it has none.
    template <class T> void remove(NodeId e) { m_world.remove<T>(e); }

    /// Calls `fn(T&...)` for every node carrying all of `Ts`. Query types may
    /// be const-qualified to ask for read-only access.
    template <class... Ts, class Fn> void each(Fn&& fn) {
        m_world.each<Ts...>(std::forward<Fn>(fn));
    }
    /// Read-only overload of each().
    template <class... Ts, class Fn> void each(Fn&& fn) const {
        m_world.each<Ts...>(std::forward<Fn>(fn));
    }

    /// Like each(), but `fn` takes the node handle first.
    template <class... Ts, class Fn> void each_node(Fn&& fn) {
        m_world.each_entity<Ts...>(std::forward<Fn>(fn));
    }
    /// Read-only overload of each_node().
    template <class... Ts, class Fn> void each_node(Fn&& fn) const {
        m_world.each_entity<Ts...>(std::forward<Fn>(fn));
    }

    /// Depth-first traversal in child order. Visitor is called for each entity
    /// including the root.
    void for_each(const std::function<void(NodeId)>& visit) const;

    /// Render every visible sprite with its world transform applied. Sprites
    /// with !texture.valid() are skipped (texture resolution is the host's job).
    /// Non-const because it refreshes the WorldTransform2D cache first, which
    /// is a write to the registry however derived the values are.
    void render_sprites(SpriteBatch& batch);

  private:
    static constexpr size_t kNoParent = static_cast<size_t>(-1);

    /// One node in the flattened traversal order, paired with the index of its
    /// parent in the same list. The order is DFS preorder, so a parent always
    /// precedes its children and its world matrix is ready by the time the
    /// child is reached.
    struct OrderEntry {
        NodeId entity{kNoNode};
        size_t parent{kNoParent};
    };

    /// One sprite awaiting its draw call, carrying the keys it is sorted by.
    struct SpriteDrawItem {
        NodeId entity{kNoNode};
        int z_order{0};
        uint32_t texture{0};
    };

    void destroy_recursive(NodeId e);
    void detach_from_parent(NodeId e);

    /// Installs the Hierarchy listeners and mints the root.
    void init_root();

    /// Subscribes mark_order_dirty to Hierarchy construction and destruction,
    /// so the traversal order is invalidated however a node appears or dies --
    /// including a world().destroy() that bypasses destroy_node.
    void connect_listeners();

    /// Repoints listeners that arrived with a swapped world from `previous`
    /// onto this scene.
    void rebind_listeners(Scene& previous);

    /// Listener signature required by ecs::Sink; both signals land here.
    void mark_order_dirty(ecs::World& world, NodeId e);

    /// Recomputes m_order from the hierarchy. Called lazily from
    /// update_world_transforms when a structural edit has invalidated it.
    void rebuild_order();

    ecs::World m_world;
    NodeId m_root{kNoNode};

    std::vector<OrderEntry> m_order;
    /// World matrix per m_order slot, so a child reads its parent's result by
    /// index instead of looking the component back up.
    std::vector<mat4> m_world_matrices;
    bool m_order_dirty{true};

    /// Sort scratch for render_sprites, kept between frames so a steady scene
    /// stops hitting the heap once the vector has grown to its working size.
    std::vector<SpriteDrawItem> m_sprite_items;
};

} // namespace vaxelis
