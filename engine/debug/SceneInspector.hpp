#pragma once

#include <entt/entt.hpp>

namespace vaxelis {

class Scene;

/// ImGui-driven scene inspector: hierarchy tree on the left, property editor
/// on the right. Call inside an ImGui frame.
class SceneInspector {
  public:
    /// Draws the inspector UI for `scene`.
    void draw(Scene& scene);

    /// Currently selected entity, or entt::null.
    entt::entity selected() const { return m_selected; }
    /// Changes the selection programmatically.
    void select(entt::entity e) { m_selected = e; }

  private:
    void draw_hierarchy(Scene& scene);
    void draw_node_recursive(Scene& scene, entt::entity e);
    void draw_properties(Scene& scene);

    entt::entity m_selected{entt::null};
    entt::entity m_pending_destroy{entt::null};
};

} // namespace vaxelis
