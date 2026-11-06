// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

#include "engine/scene/NodeId.hpp"

namespace vaxelis {

class Scene;

/// ImGui-driven scene inspector: hierarchy tree on the left, property editor
/// on the right. Call inside an ImGui frame.
class SceneInspector {
  public:
    /// Draws the inspector UI for `scene`.
    void draw(Scene& scene);

    /// Currently selected node, or kNoNode.
    NodeId selected() const { return m_selected; }
    /// Changes the selection programmatically.
    void select(NodeId e) { m_selected = e; }

  private:
    void draw_hierarchy(Scene& scene);
    void draw_node_recursive(Scene& scene, NodeId e);
    void draw_properties(Scene& scene);

    NodeId m_selected{kNoNode};
    NodeId m_pending_destroy{kNoNode};
};

} // namespace vaxelis
