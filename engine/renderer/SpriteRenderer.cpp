#include "engine/renderer/SpriteRenderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/core/Log.hpp"

namespace vaxelis {

namespace {

constexpr std::string_view kVertexSrc = R"(#version 450 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
uniform mat4 u_mvp;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);
}
)";

constexpr std::string_view kFragmentSrc = R"(#version 450 core
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 o_color;
void main() {
    o_color = texture(u_tex, v_uv);
}
)";

struct Vertex { float x, y, u, v; };

}  // namespace

bool SpriteRenderer::init(rhi::IDevice& device) {
    auto sh = device.create_shader({kVertexSrc, kFragmentSrc});
    if (!sh) { VX_ERROR("SpriteRenderer: shader create failed ({})", rhi::to_string(sh.error())); return false; }
    shader_ = *sh;

    // Unit quad centered on origin; scaled in the draw call's MVP.
    std::array<Vertex, 4> verts{{
        {-0.5f, -0.5f, 0.0f, 1.0f},
        { 0.5f, -0.5f, 1.0f, 1.0f},
        { 0.5f,  0.5f, 1.0f, 0.0f},
        {-0.5f,  0.5f, 0.0f, 0.0f},
    }};
    std::array<uint16_t, 6> idx{{0, 1, 2, 0, 2, 3}};

    auto vb = device.create_buffer({rhi::BufferUsage::Vertex, rhi::BufferAccess::Static,
                                    sizeof(verts), verts.data()});
    if (!vb) { device.destroy(shader_); return false; }
    vb_ = *vb;

    auto ib = device.create_buffer({rhi::BufferUsage::Index, rhi::BufferAccess::Static,
                                    sizeof(idx), idx.data()});
    if (!ib) { device.destroy(shader_); device.destroy(vb_); return false; }
    ib_ = *ib;
    return true;
}

void SpriteRenderer::shutdown(rhi::IDevice& device) {
    if (ib_.valid())     device.destroy(ib_);
    if (vb_.valid())     device.destroy(vb_);
    if (shader_.valid()) device.destroy(shader_);
    ib_ = {}; vb_ = {}; shader_ = {};
}

void SpriteRenderer::draw(rhi::IDevice& device, rhi::TextureHandle tex,
                          vec2 pos, vec2 size, uint32_t screen_w, uint32_t screen_h) {
    // Orthographic: pixel coords, origin top-left, Y down for screen-space convenience.
    mat4 proj  = glm::ortho(0.0f, static_cast<float>(screen_w),
                            static_cast<float>(screen_h), 0.0f, -1.0f, 1.0f);
    mat4 model = glm::translate(mat4(1.0f), vec3(pos, 0.0f)) *
                 glm::scale(mat4(1.0f), vec3(size, 1.0f));
    device.draw_textured_quad(shader_, vb_, ib_, 6, tex, proj * model);
}

}  // namespace vaxelis
