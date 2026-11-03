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
    m_root = m_registry.create();
    m_registry.emplace<Name>(m_root, "Root");
    m_registry.emplace<Hierarchy>(m_root);
    m_registry.emplace<Transform2D>(m_root);
}

entt::entity Scene::create_node(std::string name, entt::entity parent, Uuid uuid) {
    if (parent == entt::null)
        parent = m_root;
    auto e = m_registry.create();
    m_registry.emplace<Id>(e, Id{uuid.valid() ? uuid : generate_uuid()});
    m_registry.emplace<Name>(e, Name{std::move(name)});
    m_registry.emplace<Hierarchy>(e, Hierarchy{parent, {}});
    m_registry.emplace<Transform2D>(e);
    m_registry.get<Hierarchy>(parent).children.push_back(e);
    m_order_dirty = true;
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
    m_order_dirty = true;
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

    static const mat4 kIdentity(1.0f);
    for (size_t i = 0; i < m_order.size(); ++i) {
        const OrderEntry& entry = m_order[i];
        const mat4& parent_world = entry.parent == kNoParent ? kIdentity : m_world[entry.parent];
        // An entity destroyed through the registry rather than destroy_node
        // leaves a stale slot; pass the parent's matrix through so descendants
        // still land somewhere sane instead of writing to a dead entity.
        if (!m_registry.valid(entry.entity)) {
            m_world[i] = parent_world;
            continue;
        }
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

void Scene::render_sprites(SpriteBatch& batch) const {
    // Renderer expects cached world transforms; refresh them once per call.
    // The const_cast is safe: the cache is a frame-local derivation of existing
    // scene state, not a logical mutation.
    const_cast<Scene*>(this)->update_world_transforms();

    // Sort by (z_order, texture_id) so same-texture sprites at the same depth
    // collapse into a single draw call in the batcher.
    struct Item {
        entt::entity e;
        int z;
        uint32_t tex;
    };
    std::vector<Item> items;
    auto view = m_registry.view<const SpriteComponent>();
    for (auto e : view) {
        const auto& s = view.get<const SpriteComponent>(e);
        if (!s.visible || !s.texture.valid())
            continue;
        items.push_back({e, s.z_order, s.texture.id});
    }
    std::ranges::stable_sort(items, {}, [](const Item& i) { return std::pair{i.z, i.tex}; });
    for (const auto& it : items) {
        const auto& s = m_registry.get<const SpriteComponent>(it.e);
        // The whole world matrix, not just its translation column: a sprite
        // inherits the rotation and scale of every ancestor as well as its own.
        const auto& w = m_registry.get<WorldTransform2D>(it.e).matrix;
        batch.draw(s.texture, w, s.size, s.uv_rect, s.color);
    }
}

} // namespace vaxelis
