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

// Both operations are swaps, because entt::registry's own move assignment is a
// swap and its move constructor rebinds only the destination: a scene whose
// registry was moved out of is not something entt promises is usable. Trading
// contents instead leaves both ends valid, and the source -- a temporary, in
// the `m_scene = Scene{}` idiom -- takes the old scene to the grave with it.
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
    m_registry.swap(other.m_registry);
    swap(m_root, other.m_root);
    swap(m_order, other.m_order);
    swap(m_world, other.m_world);
    swap(m_order_dirty, other.m_order_dirty);
    swap(m_sprite_items, other.m_sprite_items);
    // The connections live in the component pools, so they travelled with the
    // registries: each side's listeners now name the other scene.
    rebind_listeners(other);
    other.rebind_listeners(*this);
}

void Scene::init_root() {
    // Listeners first: emplacing the root's Hierarchy below fires one.
    connect_listeners();
    m_root = m_registry.create();
    m_registry.emplace<Name>(m_root, "Root");
    m_registry.emplace<Hierarchy>(m_root);
    m_registry.emplace<Transform2D>(m_root);
}

void Scene::connect_listeners() {
    m_registry.on_construct<Hierarchy>().connect<&Scene::mark_order_dirty>(*this);
    m_registry.on_destroy<Hierarchy>().connect<&Scene::mark_order_dirty>(*this);
}

void Scene::rebind_listeners(Scene& previous) {
    // The connections came across with the registry still naming `previous`.
    m_registry.on_construct<Hierarchy>().disconnect(&previous);
    m_registry.on_destroy<Hierarchy>().disconnect(&previous);
    connect_listeners();
}

void Scene::mark_order_dirty(entt::registry&, entt::entity) {
    m_order_dirty = true;
}

entt::entity Scene::create_node(std::string name, entt::entity parent, Uuid uuid) {
    if (parent == entt::null)
        parent = m_root;
    auto e = m_registry.create();
    m_registry.emplace<Id>(e, Id{uuid.valid() ? uuid : generate_uuid()});
    m_registry.emplace<Name>(e, Name{std::move(name)});
    m_registry.emplace<Hierarchy>(e, Hierarchy{parent, {}});
    m_registry.emplace<Transform2D>(e);
    // No explicit invalidation here or in destroy_node: emplacing and releasing
    // Hierarchy is what the listeners watch.
    m_registry.get<Hierarchy>(parent).children.push_back(e);
    return e;
}

entt::entity Scene::find_by_uuid(const Uuid& uuid) const {
    if (!uuid.valid())
        return entt::null;
    for (auto e : m_registry.view<const Id>())
        if (m_registry.get<const Id>(e).uuid == uuid)
            return e;
    return entt::null;
}

void Scene::detach_from_parent(entt::entity e) {
    auto* h = m_registry.try_get<Hierarchy>(e);
    if (!h || h->parent == entt::null)
        return;
    auto& parent_h = m_registry.get<Hierarchy>(h->parent);
    std::erase(parent_h.children, e);
    h->parent = entt::null;
}

void Scene::destroy_recursive(entt::entity e) {
    auto* h = m_registry.try_get<Hierarchy>(e);
    if (h) {
        // Copy because destroy invalidates the child list.
        auto kids = h->children;
        for (auto c : kids)
            destroy_recursive(c);
    }
    m_registry.destroy(e);
}

void Scene::destroy_node(entt::entity e) {
    if (e == entt::null || e == m_root || !m_registry.valid(e))
        return;
    detach_from_parent(e);
    destroy_recursive(e);
}

void Scene::set_parent(entt::entity e, entt::entity new_parent) {
    if (e == entt::null || e == m_root)
        return;
    if (new_parent == entt::null)
        new_parent = m_root;
    if (new_parent == e)
        return;

    // Cycle check: walk new_parent up to root, reject if e is on the path.
    for (auto cur = new_parent; cur != entt::null;) {
        if (cur == e)
            return;
        auto* h = m_registry.try_get<Hierarchy>(cur);
        cur = h ? h->parent : entt::null;
    }

    detach_from_parent(e);
    auto& eh = m_registry.get<Hierarchy>(e);
    eh.parent = new_parent;
    m_registry.get<Hierarchy>(new_parent).children.push_back(e);
    // Reparenting mutates Hierarchy in place, so no signal fires for it.
    m_order_dirty = true;
}

mat4 Scene::world_matrix(entt::entity e) const {
    if (e == entt::null || !m_registry.valid(e))
        return mat4(1.0f);
    // Prefer cached value if update_world_transforms was called this frame.
    if (const auto* w = m_registry.try_get<WorldTransform2D>(e))
        return w->matrix;
    const auto* t = m_registry.try_get<Transform2D>(e);
    const auto* h = m_registry.try_get<Hierarchy>(e);
    mat4 m = t ? t->local_matrix() : mat4(1.0f);
    if (h && h->parent != entt::null)
        m = world_matrix(h->parent) * m;
    return m;
}

void Scene::rebuild_order() {
    m_order.clear();
    // Iterative DFS: an explicit stack keeps a deep tree off the call stack.
    // Children are pushed in reverse so they pop back in authoring order.
    std::vector<std::pair<entt::entity, size_t>> stack;
    stack.emplace_back(m_root, kNoParent);
    while (!stack.empty()) {
        const auto [e, parent] = stack.back();
        stack.pop_back();
        // A child list can still name an entity destroyed straight through the
        // registry, which never got the chance to unlink itself. Skipping it
        // here is what keeps the sweep free of liveness checks.
        if (!m_registry.valid(e))
            continue;
        const size_t index = m_order.size();
        m_order.push_back(OrderEntry{e, parent});
        if (const auto* h = m_registry.try_get<Hierarchy>(e)) {
            for (auto c : std::ranges::reverse_view(h->children))
                stack.emplace_back(c, index);
        }
    }
    m_world.assign(m_order.size(), mat4(1.0f));
    m_order_dirty = false;
}

void Scene::update_world_transforms() {
    if (m_order_dirty)
        rebuild_order();

    // Every entry is live: a node cannot die without releasing its Hierarchy,
    // and that fires the listener that forces the rebuild above.
    static const mat4 kIdentity(1.0f);
    for (size_t i = 0; i < m_order.size(); ++i) {
        const OrderEntry& entry = m_order[i];
        const mat4& parent_world = entry.parent == kNoParent ? kIdentity : m_world[entry.parent];
        mat4 world = parent_world;
        if (const auto* t = m_registry.try_get<Transform2D>(entry.entity))
            world = parent_world * t->local_matrix();
        m_world[i] = world;
        m_registry.emplace_or_replace<WorldTransform2D>(entry.entity, world);
    }
}

void Scene::for_each(const std::function<void(entt::entity)>& visit) const {
    auto walk = [&](auto& self, entt::entity e) -> void {
        visit(e);
        const auto* h = m_registry.try_get<Hierarchy>(e);
        if (!h)
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
    auto view = m_registry.view<const SpriteComponent>();
    for (auto e : view) {
        const auto& s = view.get<const SpriteComponent>(e);
        if (!s.visible || !s.texture.valid())
            continue;
        m_sprite_items.push_back(SpriteDrawItem{e, s.z_order, s.texture.id});
    }
    std::ranges::stable_sort(m_sprite_items, {}, [](const SpriteDrawItem& i) {
        return std::pair{i.z_order, i.texture};
    });
    for (const auto& it : m_sprite_items) {
        const auto& s = m_registry.get<const SpriteComponent>(it.entity);
        // The whole world matrix, not just its translation column: a sprite
        // inherits the rotation and scale of every ancestor as well as its own.
        const auto& w = m_registry.get<WorldTransform2D>(it.entity).matrix;
        batch.draw(s.texture, w, s.size, s.uv_rect, s.color);
    }
}

} // namespace vaxelis
