#include "engine/scene/Scene.hpp"

#include <algorithm>

#include "engine/renderer/SpriteRenderer.hpp"

namespace vaxelis {

Scene::Scene() {
    root_ = registry_.create();
    registry_.emplace<Name>(root_, "Root");
    registry_.emplace<Hierarchy>(root_);
    registry_.emplace<Transform2D>(root_);
}

entt::entity Scene::create_node(std::string name, entt::entity parent) {
    if (parent == entt::null) parent = root_;
    auto e = registry_.create();
    registry_.emplace<Name>(e, Name{std::move(name)});
    registry_.emplace<Hierarchy>(e, Hierarchy{parent, {}});
    registry_.emplace<Transform2D>(e);
    registry_.get<Hierarchy>(parent).children.push_back(e);
    return e;
}

void Scene::detach_from_parent(entt::entity e) {
    auto* h = registry_.try_get<Hierarchy>(e);
    if (!h || h->parent == entt::null) return;
    auto& parent_h = registry_.get<Hierarchy>(h->parent);
    std::erase(parent_h.children, e);
    h->parent = entt::null;
}

void Scene::destroy_recursive(entt::entity e) {
    auto* h = registry_.try_get<Hierarchy>(e);
    if (h) {
        // Copy because destroy invalidates the child list.
        auto kids = h->children;
        for (auto c : kids) destroy_recursive(c);
    }
    registry_.destroy(e);
}

void Scene::destroy_node(entt::entity e) {
    if (e == entt::null || e == root_ || !registry_.valid(e)) return;
    detach_from_parent(e);
    destroy_recursive(e);
}

void Scene::set_parent(entt::entity e, entt::entity new_parent) {
    if (e == entt::null || e == root_) return;
    if (new_parent == entt::null) new_parent = root_;
    if (new_parent == e) return;

    // Cycle check: walk new_parent up to root, reject if e is on the path.
    for (auto cur = new_parent; cur != entt::null; ) {
        if (cur == e) return;
        auto* h = registry_.try_get<Hierarchy>(cur);
        cur = h ? h->parent : entt::null;
    }

    detach_from_parent(e);
    auto& eh = registry_.get<Hierarchy>(e);
    eh.parent = new_parent;
    registry_.get<Hierarchy>(new_parent).children.push_back(e);
}

mat4 Scene::world_matrix(entt::entity e) const {
    if (e == entt::null || !registry_.valid(e)) return mat4(1.0f);
    const auto* t = registry_.try_get<Transform2D>(e);
    const auto* h = registry_.try_get<Hierarchy>(e);
    mat4 m = t ? t->local_matrix() : mat4(1.0f);
    if (h && h->parent != entt::null) m = world_matrix(h->parent) * m;
    return m;
}

void Scene::for_each(const std::function<void(entt::entity)>& visit) const {
    auto walk = [&](auto& self, entt::entity e) -> void {
        visit(e);
        const auto* h = registry_.try_get<Hierarchy>(e);
        if (!h) return;
        for (auto c : h->children) self(self, c);
    };
    walk(walk, root_);
}

void Scene::render_sprites(SpriteBatch& batch) const {
    // Gather visible sprites, sort by z_order, render.
    struct Item {
        entt::entity e;
        int z;
    };
    std::vector<Item> items;
    auto view = registry_.view<const SpriteComponent>();
    // entt's view::size_hint is only available for in-place storage; skip the
    // hint and let the vector grow.
    for (auto e : view) {
        const auto& s = view.get<const SpriteComponent>(e);
        if (!s.visible || !s.texture.valid()) continue;
        items.push_back({e, s.z_order});
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.z < b.z; });
    for (const auto& it : items) {
        const auto& s = registry_.get<const SpriteComponent>(it.e);
        const mat4 w  = world_matrix(it.e);
        const vec2 pos{w[3].x, w[3].y};
        batch.draw(s.texture, pos, s.size, s.uv_rect, s.color);
    }
}

}  // namespace vaxelis
