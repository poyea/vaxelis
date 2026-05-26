#pragma once

#include <vector>

#include "engine/rhi/Rhi.hpp"

namespace vaxelis {

// Batches textured quads into a single dynamic VBO. Flushes on texture change,
// end(), or when the per-batch quad cap is hit.
//
// Usage:
//   batch.begin(screen_w, screen_h);
//   batch.draw(tex, pos, size);
//   batch.draw(tex2, ...);
//   batch.end();
class SpriteBatch {
public:
    static constexpr uint32_t kDefaultMaxQuads = 4096;

    bool init(rhi::IDevice& device, uint32_t max_quads = kDefaultMaxQuads);
    void shutdown(rhi::IDevice& device);

    // Set up the screen-space orthographic projection for this frame.
    void begin(rhi::IDevice& device, uint32_t screen_w, uint32_t screen_h);

    // Custom projection (e.g. from Camera2D::projection()).
    void begin(rhi::IDevice& device, const mat4& projection);
    void end();

    // Centered quad. `uv` defaults to full texture (0,0)-(1,1).
    void draw(rhi::TextureHandle, vec2 pos, vec2 size,
              vec4 color = vec4(1.0f));
    void draw(rhi::TextureHandle, vec2 pos, vec2 size,
              vec4 uv_rect /* min_u, min_v, max_u, max_v */, vec4 color);

    // Stats from the most recently ended frame.
    uint32_t draw_calls() const { return last_draw_calls_; }
    uint32_t quads()      const { return last_quads_; }

private:
    struct Vertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    void flush();

    rhi::IDevice*     device_{nullptr};
    rhi::ShaderHandle shader_{};
    rhi::BufferHandle vb_{};
    rhi::BufferHandle ib_{};
    uint32_t          max_quads_{0};
    std::vector<Vertex> verts_;
    rhi::TextureHandle current_tex_{};
    mat4 proj_{1.0f};
    bool in_frame_{false};

    uint32_t draw_calls_{0};
    uint32_t quads_{0};
    uint32_t last_draw_calls_{0};
    uint32_t last_quads_{0};
};

}  // namespace vaxelis
