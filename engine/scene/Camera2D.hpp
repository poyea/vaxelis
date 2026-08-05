#pragma once

#include "engine/math/Math.hpp"

namespace vaxelis {

/// Screen-space 2D camera. Position is the world point at the screen center.
/// `zoom` > 1 magnifies. Y-down to match the rest of the renderer.
struct Camera2D {
    vec2 position{0.0f, 0.0f};
    float zoom{1.0f};

    /// Optional world-space clamp; if both extents are zero, no clamping.
    vec2 bounds_min{0.0f, 0.0f};
    /// See bounds_min.
    vec2 bounds_max{0.0f, 0.0f};

    /// World-space rect the camera covers at this framebuffer size, in pixels.
    /// Useful for culling; `min` is the top-left corner (y-down).
    AABB2 visible_bounds(uint32_t screen_w, uint32_t screen_h) const {
        const float hw = (static_cast<float>(screen_w) * 0.5f) / zoom;
        const float hh = (static_cast<float>(screen_h) * 0.5f) / zoom;
        return {{position.x - hw, position.y - hh}, {position.x + hw, position.y + hh}};
    }

    /// Orthographic projection that maps the visible world rect to NDC. Pass
    /// the framebuffer size in pixels.
    mat4 projection(uint32_t screen_w, uint32_t screen_h) const {
        const AABB2 v = visible_bounds(screen_w, screen_h);
        return glm::ortho(v.min.x, v.max.x, v.max.y, v.min.y, -1.0f, 1.0f);
    }

    /// Clamps `position` so the camera's visible rect stays inside [bounds_min,
    /// bounds_max]. An axis whose bounded extent is smaller than the view is
    /// centered on the bounds instead. No-op when bounds are degenerate.
    void apply_bounds(uint32_t screen_w, uint32_t screen_h) {
        if (bounds_min == bounds_max)
            return;
        const float hw = (static_cast<float>(screen_w) * 0.5f) / zoom;
        const float hh = (static_cast<float>(screen_h) * 0.5f) / zoom;
        const float min_x = bounds_min.x + hw;
        const float max_x = bounds_max.x - hw;
        const float min_y = bounds_min.y + hh;
        const float max_y = bounds_max.y - hh;
        position.x = max_x > min_x ? glm::clamp(position.x, min_x, max_x)
                                   : (bounds_min.x + bounds_max.x) * 0.5f;
        position.y = max_y > min_y ? glm::clamp(position.y, min_y, max_y)
                                   : (bounds_min.y + bounds_max.y) * 0.5f;
    }
};

} // namespace vaxelis
