#include "engine/scene/Scene.hpp"

#include <algorithm>
#include <utility>

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

void Scene::update_world_transforms() {
    auto walk = [&](auto& self, entt::entity e, const mat4& parent_world) -> void {
        if (!m_registry.valid(e))
            return;
        mat4 world = parent_world;
        if (const auto* t = m_registry.try_get<Transform2D>(e)) {
            world = parent_world * t->local_matrix();
        }
        m_registry.emplace_or_replace<WorldTransform2D>(e, world);
        if (const auto* h = m_registry.try_get<Hierarchy>(e)) {
            for (auto c : h->children)
                self(self, c, world);
        }
    };
    walk(walk, m_root, mat4(1.0f));
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
        const auto& w = m_registry.get<WorldTransform2D>(it.e).matrix;
        const vec2 pos{w[3].x, w[3].y};
        batch.draw(s.texture, pos, s.size, s.uv_rect, s.color);
    }
}

} // namespace vaxelis
