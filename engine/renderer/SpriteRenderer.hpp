#pragma once

#include "engine/rhi/Rhi.hpp"

namespace vaxelis {

// Minimal M1 sprite renderer: one textured quad with an orthographic camera.
// No batching yet.
class SpriteRenderer {
public:
    // Constructs GPU resources. Returns false if shader/buffer creation failed.
    bool init(rhi::IDevice& device);
    void shutdown(rhi::IDevice& device);

    // Draws one quad centered at `pos` with size `size` (pixels) using `tex`.
    // Caller is responsible for begin_frame/end_frame.
    void draw(rhi::IDevice& device, rhi::TextureHandle tex,
              vec2 pos, vec2 size, uint32_t screen_w, uint32_t screen_h);

private:
    rhi::ShaderHandle shader_{};
    rhi::BufferHandle vb_{};
    rhi::BufferHandle ib_{};
};

}  // namespace vaxelis
