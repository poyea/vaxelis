// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/scene/Scene.hpp"

#include <algorithm>
#include <ranges>
#include <utility>
#include <vector>

#include "engine/renderer/SpriteRenderer.hpp"

namespace vaxelis {

Scene::Scene() {
    init_root();
}

// Both operations are swaps. Trading contents leaves both ends valid, and the
// source -- a temporary, in the `m_scene = Scene{}` idiom -- takes the old
// scene to the grave with it.
Scene::Scene(Scene&& other) : Scene() {
    swap(other);
}

Scene& Scene::operator=(Scene&& other) noexcept {
    if (this != &other)
        swap(other);
    return *this;
}

void Scene::swap(Scene& other) noexcept {
    using std::swap;
    // std::swap over the worlds, which are movable but have no swap of their
    // own; three moves, all noexcept.
    swap(m_world, other.m_world);
    swap(m_root, other.m_root);
    swap(m_order, other.m_order);
    swap(m_world_matrices, other.m_world_matrices);
    swap(m_order_dirty, other.m_order_dirty);
    swap(m_sprite_items, other.m_sprite_items);
    // Subscriptions travelled with the worlds, so each side's listeners now
    // name the other scene.
    rebind_listeners(other);
    other.rebind_listeners(*this);
}

void Scene::init_root() {
    // Listeners first: creating the root below fires one.
    connect_listeners();
    // One variadic create, not a create plus three adds: each add would migrate
    // the row to a new archetype, and the benchmark puts that at 6.28x entt
    // against 2.81x for this.
    m_root = m_world.create(Name{"Root"}, Hierarchy{}, Transform2D{}, WorldTransform2D{});
}

void Scene::connect_listeners() {
    m_world.on_construct<Hierarchy>().connect<&Scene::mark_order_dirty>(*this);
    m_world.on_destroy<Hierarchy>().connect<&Scene::mark_order_dirty>(*this);
}

void Scene::rebind_listeners(Scene& previous) {
    // The connections came across with the world still naming `previous`.
    m_world.on_construct<Hierarchy>().disconnect(&previous);
    m_world.on_destroy<Hierarchy>().disconnect(&previous);
    connect_listeners();
}

void Scene::mark_order_dirty(ecs::World&, NodeId) {
    m_order_dirty = true;
}

NodeId Scene::create_node(std::string name, NodeId parent, Uuid uuid) {
    if (parent == kNoNode)
        parent = m_root;
    // A handle to a destroyed node is the normal case for a caller holding a
    // selection across a delete. Re-homing under the root keeps the tree
    // connected; creating first and failing to link afterwards would leave an
    // orphan carrying an Id and a Name.
    if (!has<Hierarchy>(parent))
        parent = m_root;
    // Every component in one create: see init_root on why this is not four
    // add() calls. WorldTransform2D is in the set so the per-frame sweep only
    // ever overwrites it, never migrates a row to make room for it.
    const NodeId e =
        m_world.create(Id{uuid.valid() ? uuid : generate_uuid()}, Name{std::move(name)},
                       Hierarchy{parent, {}}, Transform2D{}, WorldTransform2D{});
    // No explicit invalidation here or in destroy_node: a Hierarchy coming into
    // or going out of existence is what the listeners watch.
    get<Hierarchy>(parent).children.push_back(e);
    return e;
}

NodeId Scene::find_by_uuid(const Uuid& uuid) const {
    if (!uuid.valid())
        return kNoNode;
    // No early exit out of a query walk, so carry the answer out instead.
    NodeId found = kNoNode;
    each_node<const Id>([&](NodeId e, const Id& id) {
        if (found == kNoNode && id.uuid == uuid)
            found = e;
    });
    return found;
}

void Scene::detach_from_parent(NodeId e) {
    auto* h = try_get<Hierarchy>(e);
    if (h == nullptr || h->parent == kNoNode)
        return;
    if (auto* parent_h = try_get<Hierarchy>(h->parent))
        std::erase(parent_h->children, e);
    h->parent = kNoNode;
}

void Scene::destroy_recursive(NodeId e) {
    if (auto* h = try_get<Hierarchy>(e)) {
        // Copy because destroy invalidates the child list.
        auto kids = h->children;
        for (auto c : kids)
            destroy_recursive(c);
    }
    m_world.destroy(e);
}

void Scene::destroy_node(NodeId e) {
    if (e == m_root || !valid(e))
        return;
    detach_from_parent(e);
    destroy_recursive(e);
}

void Scene::set_parent(NodeId e, NodeId new_parent) {
    if (e == m_root || !has<Hierarchy>(e))
        return;
    if (new_parent == kNoNode)
        new_parent = m_root;
    if (new_parent == e)
        return;
    // Checked before detaching, not after: unlinking `e` and then failing to
    // attach it would leave it parentless and off every child list.
    if (!has<Hierarchy>(new_parent))
        return;

    // Cycle check: walk new_parent up to root, reject if e is on the path.
    for (auto cur = new_parent; cur != kNoNode;) {
        if (cur == e)
            return;
        auto* h = try_get<Hierarchy>(cur);
        cur = h != nullptr ? h->parent : kNoNode;
    }

    detach_from_parent(e);
    get<Hierarchy>(e).parent = new_parent;
    get<Hierarchy>(new_parent).children.push_back(e);
    // Reparenting mutates Hierarchy in place, so no signal fires for it.
    m_order_dirty = true;
}

mat4 Scene::world_matrix(NodeId e) const {
    if (!valid(e))
        return mat4(1.0f);
    const auto* t = try_get<Transform2D>(e);
    const auto* h = try_get<Hierarchy>(e);
    mat4 m = t ? t->local_matrix() : mat4(1.0f);
    if (h && h->parent != kNoNode)
        m = world_matrix(h->parent) * m;
    return m;
}

void Scene::rebuild_order() {
    m_order.clear();
    // Iterative DFS: an explicit stack keeps a deep tree off the call stack.
    // Children are pushed in reverse so they pop back in authoring order.
    std::vector<std::pair<NodeId, size_t>> stack;
    stack.emplace_back(m_root, kNoParent);
    while (!stack.empty()) {
        const auto [e, parent] = stack.back();
        stack.pop_back();
        // A child list can still name a node destroyed straight through the
        // world, which never got the chance to unlink itself. Skipping it here
        // is what keeps the sweep free of liveness checks.
        if (!valid(e))
            continue;
        const size_t index = m_order.size();
        m_order.push_back(OrderEntry{e, parent});
        if (const auto* h = try_get<Hierarchy>(e)) {
            for (auto c : std::ranges::reverse_view(h->children))
                stack.emplace_back(c, index);
        }
    }
    m_world_matrices.assign(m_order.size(), mat4(1.0f));
    m_order_dirty = false;
}

void Scene::update_world_transforms() {
    if (m_order_dirty)
        rebuild_order();

    // Nearly every entry is live, because a node normally cannot die without
    // releasing its Hierarchy and firing the rebuild above. One path dodges
    // that: remove<Hierarchy>() followed by world().destroy() publishes nothing
    // about Hierarchy at all. Pass the parent's matrix through rather than
    // writing to a dead handle.
    static const mat4 kIdentity(1.0f);
    for (size_t i = 0; i < m_order.size(); ++i) {
        const OrderEntry& entry = m_order[i];
        const mat4& parent_world =
            entry.parent == kNoParent ? kIdentity : m_world_matrices[entry.parent];
        if (!valid(entry.entity)) {
            m_world_matrices[i] = parent_world;
            continue;
        }
        mat4 world = parent_world;
        if (const auto* t = try_get<Transform2D>(entry.entity))
            world = parent_world * t->local_matrix();
        m_world_matrices[i] = world;
        // create_node gives every node a WorldTransform2D, so this is an
        // overwrite. A node built straight through the world may lack one, and
        // only that case pays a migration -- once.
        if (auto* cached = try_get<WorldTransform2D>(entry.entity))
            cached->matrix = world;
        else
            add<WorldTransform2D>(entry.entity, world);
    }
}

void Scene::for_each(const std::function<void(NodeId)>& visit) const {
    auto walk = [&](auto& self, NodeId e) -> void {
        // A child list can outlive its child, the same way rebuild_order has to
        // allow for. Serialization walks through here, so visiting a dead handle
        // would fault on the first component read.
        if (!valid(e))
            return;
        visit(e);
        const auto* h = try_get<Hierarchy>(e);
        if (h == nullptr)
            return;
        for (auto c : h->children)
            self(self, c);
    };
    walk(walk, m_root);
}

void Scene::render_sprites(SpriteBatch& batch) {
    // Renderer expects cached world transforms; refresh them once per call.
    update_world_transforms();

    // Sort by (z_order, texture_id) so same-texture sprites at the same depth
    // collapse into a single draw call in the batcher. The scratch vector is a
    // member: clear() keeps the capacity, so only a scene that grows past its
    // previous sprite count allocates.
    m_sprite_items.clear();
    each_node<const SpriteComponent>([this](NodeId e, const SpriteComponent& s) {
        if (!s.visible || !s.texture.valid())
            return;
        m_sprite_items.push_back(SpriteDrawItem{e, s.z_order, s.texture.id});
    });
    std::ranges::stable_sort(m_sprite_items, {}, [](const SpriteDrawItem& i) {
        return std::pair{i.z_order, i.texture};
    });
    for (const auto& it : m_sprite_items) {
        const auto& s = get<SpriteComponent>(it.entity);
        // Only nodes the sweep reached carry a cached transform, and a sprite
        // built straight through world() reaches nothing. Skipping it beats
        // dereferencing a component it does not have.
        const auto* w = try_get<WorldTransform2D>(it.entity);
        if (w == nullptr)
            continue;
        // The whole world matrix, not just its translation column: a sprite
        // inherits the rotation and scale of every ancestor as well as its own.
        batch.draw(s.texture, w->matrix, s.size, s.uv_rect, s.color);
    }
}

} // namespace vaxelis
