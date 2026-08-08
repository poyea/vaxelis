#pragma once

#include <vector>

#include "engine/rhi/Rhi.hpp"

namespace vaxelis {

/// Batches textured quads into a single dynamic VBO. Flushes on texture change,
/// end(), or when the per-batch quad cap is hit.
///
/// Usage:
/// @code
///   batch.begin(screen_w, screen_h);
///   batch.draw(tex, pos, size);
///   batch.draw(tex2, ...);
///   batch.end();
/// @endcode
class SpriteBatch {
  public:
    static constexpr uint32_t kDefaultMaxQuads = 4096;

    /// Creates the shader and vertex/index buffers.
    bool init(rhi::IDevice& device, uint32_t max_quads = kDefaultMaxQuads);
    /// Destroys the GPU resources created by init().
    void shutdown(rhi::IDevice& device);

    /// Set up the screen-space orthographic projection for this frame.
    void begin(rhi::IDevice& device, uint32_t screen_w, uint32_t screen_h);

    /// Custom projection (e.g. from Camera2D::projection()).
    void begin(rhi::IDevice& device, const mat4& projection);
    /// Flushes any pending quads and finalizes this frame's stats.
    void end();

    /// Centered quad. `uv` defaults to full texture (0,0)-(1,1).
    void draw(rhi::TextureHandle, vec2 pos, vec2 size, vec4 color = vec4(1.0f));
    /// Centered quad with an explicit UV sub-rectangle.
    void draw(rhi::TextureHandle, vec2 pos, vec2 size,
              vec4 uv_rect /* min_u, min_v, max_u, max_v */, vec4 color);

    /// Draw calls submitted in the most recently ended frame.
    uint32_t draw_calls() const { return m_last_draw_calls; }
    /// Quads drawn in the most recently ended frame.
    uint32_t quads() const { return m_last_quads; }

  private:
    struct Vertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    void flush();

    rhi::IDevice* m_device{nullptr};
    rhi::ShaderHandle m_shader{};
    rhi::BufferHandle m_vb{};
    rhi::BufferHandle m_ib{};
    uint32_t m_max_quads{0};
    std::vector<Vertex> m_verts;
    rhi::TextureHandle m_current_tex{};
    mat4 m_proj{1.0f};
    bool m_in_frame{false};

    uint32_t m_draw_calls{0};
    uint32_t m_quads{0};
    uint32_t m_last_draw_calls{0};
    uint32_t m_last_quads{0};
};

} // namespace vaxelis
