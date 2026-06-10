#include "engine/scene/Scene.hpp"

#include <algorithm>
#include <utility>

#include "engine/renderer/SpriteRenderer.hpp"

namespace vaxelis {

Scene::Scene() {
    root_ = registry_.create();
    registry_.emplace<Name>(root_, "Root");
    registry_.emplace<Hierarchy>(root_);
    registry_.emplace<Transform2D>(root_);
}

entt::entity Scene::create_node(std::string name, entt::entity parent, Uuid uuid) {
    if (parent == entt::null)
        parent = root_;
    auto e = registry_.create();
    registry_.emplace<Id>(e, Id{uuid.valid() ? uuid : generate_uuid()});
    registry_.emplace<Name>(e, Name{std::move(name)});
    registry_.emplace<Hierarchy>(e, Hierarchy{parent, {}});
    registry_.emplace<Transform2D>(e);
    registry_.get<Hierarchy>(parent).children.push_back(e);
    return e;
}

entt::entity Scene::find_by_uuid(const Uuid& uuid) const {
    if (!uuid.valid())
        return entt::null;
    for (auto e : registry_.view<const Id>())
        if (registry_.get<const Id>(e).uuid == uuid)
            return e;
    return entt::null;
}

void Scene::detach_from_parent(entt::entity e) {
    auto* h = registry_.try_get<Hierarchy>(e);
    if (!h || h->parent == entt::null)
        return;
    auto& parent_h = registry_.get<Hierarchy>(h->parent);
    std::erase(parent_h.children, e);
    h->parent = entt::null;
}

void Scene::destroy_recursive(entt::entity e) {
    auto* h = registry_.try_get<Hierarchy>(e);
    if (h) {
        // Copy because destroy invalidates the child list.
        auto kids = h->children;
        for (auto c : kids)
            destroy_recursive(c);
    }
    registry_.destroy(e);
}

void Scene::destroy_node(entt::entity e) {
    if (e == entt::null || e == root_ || !registry_.valid(e))
        return;
    detach_from_parent(e);
    destroy_recursive(e);
}

void Scene::set_parent(entt::entity e, entt::entity new_parent) {
    if (e == entt::null || e == root_)
        return;
    if (new_parent == entt::null)
        new_parent = root_;
    if (new_parent == e)
        return;

    // Cycle check: walk new_parent up to root, reject if e is on the path.
    for (auto cur = new_parent; cur != entt::null;) {
        if (cur == e)
            return;
        auto* h = registry_.try_get<Hierarchy>(cur);
        cur = h ? h->parent : entt::null;
    }

    detach_from_parent(e);
    auto& eh = registry_.get<Hierarchy>(e);
    eh.parent = new_parent;
    registry_.get<Hierarchy>(new_parent).children.push_back(e);
}

mat4 Scene::world_matrix(entt::entity e) const {
    if (e == entt::null || !registry_.valid(e))
        return mat4(1.0f);
    // Prefer cached value if update_world_transforms was called this frame.
    if (const auto* w = registry_.try_get<WorldTransform2D>(e))
        return w->matrix;
    const auto* t = registry_.try_get<Transform2D>(e);
    const auto* h = registry_.try_get<Hierarchy>(e);
    mat4 m = t ? t->local_matrix() : mat4(1.0f);
    if (h && h->parent != entt::null)
        m = world_matrix(h->parent) * m;
    return m;
}

void Scene::update_world_transforms() {
    auto walk = [&](auto& self, entt::entity e, const mat4& parent_world) -> void {
        if (!registry_.valid(e))
            return;
        mat4 world = parent_world;
        if (const auto* t = registry_.try_get<Transform2D>(e)) {
            world = parent_world * t->local_matrix();
        }
        registry_.emplace_or_replace<WorldTransform2D>(e, world);
        if (const auto* h = registry_.try_get<Hierarchy>(e)) {
            for (auto c : h->children)
                self(self, c, world);
        }
    };
    walk(walk, root_, mat4(1.0f));
}

void Scene::for_each(const std::function<void(entt::entity)>& visit) const {
    auto walk = [&](auto& self, entt::entity e) -> void {
        visit(e);
        const auto* h = registry_.try_get<Hierarchy>(e);
        if (!h)
            return;
        for (auto c : h->children)
            self(self, c);
    };
    walk(walk, root_);
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
    auto view = registry_.view<const SpriteComponent>();
    for (auto e : view) {
        const auto& s = view.get<const SpriteComponent>(e);
        if (!s.visible || !s.texture.valid())
            continue;
        items.push_back({e, s.z_order, s.texture.id});
    }
    std::ranges::stable_sort(items, {}, [](const Item& i) { return std::pair{i.z, i.tex}; });
    for (const auto& it : items) {
        const auto& s = registry_.get<const SpriteComponent>(it.e);
        const auto& w = registry_.get<WorldTransform2D>(it.e).matrix;
        const vec2 pos{w[3].x, w[3].y};
        batch.draw(s.texture, pos, s.size, s.uv_rect, s.color);
    }
}

} // namespace vaxelis
