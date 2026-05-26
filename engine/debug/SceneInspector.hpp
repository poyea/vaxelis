#pragma once

#include <entt/entt.hpp>

namespace vaxelis {

class Scene;

// ImGui-driven scene inspector: hierarchy tree on the left, property editor
// on the right. Call inside an ImGui frame.
class SceneInspector {
public:
    void draw(Scene& scene);

    entt::entity selected() const { return selected_; }
    void  select(entt::entity e)  { selected_ = e; }

private:
    void draw_hierarchy(Scene& scene);
    void draw_node_recursive(Scene& scene, entt::entity e);
    void draw_properties(Scene& scene);

    entt::entity selected_{entt::null};
    entt::entity pending_destroy_{entt::null};
};

}  // namespace vaxelis
