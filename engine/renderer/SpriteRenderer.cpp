#include "engine/renderer/SpriteRenderer.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/core/Log.hpp"

namespace vaxelis {

namespace {

// GLSL 4.50 core (desktop) and GLSL ES 3.00 (WebGL2/Emscripten) variants. The
// only differences are the #version line and the fragment shader's required
// `precision` statement; the attribute layout(location=) qualifiers, plain
// uniforms, and unqualified varyings are all valid in both dialects.
#ifdef __EMSCRIPTEN__
constexpr std::string_view kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform mat4 u_mvp;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);
}
)";

constexpr std::string_view kFragmentSrc = R"(#version 300 es
precision mediump float;
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_tex;
out vec4 o_color;
void main() {
    o_color = texture(u_tex, v_uv) * v_color;
}
)";
#else
constexpr std::string_view kVertexSrc = R"(#version 450 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform mat4 u_mvp;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);
}
)";

constexpr std::string_view kFragmentSrc = R"(#version 450 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_tex;
out vec4 o_color;
void main() {
    o_color = texture(u_tex, v_uv) * v_color;
}
)";
#endif

} // namespace

bool SpriteBatch::init(rhi::IDevice& device, uint32_t max_quads) {
    max_quads_ = max_quads;
    verts_.reserve(static_cast<size_t>(max_quads_) * 4);

    auto sh = device.create_shader({kVertexSrc, kFragmentSrc});
    if (!sh) {
        VX_ERROR("SpriteBatch: shader create failed ({})", rhi::to_string(sh.error()));
        return false;
    }
    shader_ = *sh;

    auto vb = device.create_buffer({rhi::BufferUsage::Vertex, rhi::BufferAccess::Dynamic,
                                    sizeof(Vertex) * max_quads_ * 4, nullptr});
    if (!vb) {
        device.destroy(shader_);
        return false;
    }
    vb_ = *vb;

    // Pre-build the index buffer: each quad is 6 indices (two triangles) in a
    // fixed pattern, so it can live in a static buffer for the lifetime of the
    // batcher.
    std::vector<uint16_t> indices(static_cast<size_t>(max_quads_) * 6);
    for (uint32_t q = 0; q < max_quads_; ++q) {
        const uint16_t base = static_cast<uint16_t>(q * 4);
        const size_t i = static_cast<size_t>(q) * 6;
        indices[i + 0] = base + 0;
        indices[i + 1] = base + 1;
        indices[i + 2] = base + 2;
        indices[i + 3] = base + 0;
        indices[i + 4] = base + 2;
        indices[i + 5] = base + 3;
    }
    auto ib = device.create_buffer({rhi::BufferUsage::Index, rhi::BufferAccess::Static,
                                    indices.size() * sizeof(uint16_t), indices.data()});
    if (!ib) {
        device.destroy(shader_);
        device.destroy(vb_);
        return false;
    }
    ib_ = *ib;
    return true;
}

void SpriteBatch::shutdown(rhi::IDevice& device) {
    if (ib_.valid())
        device.destroy(ib_);
    if (vb_.valid())
        device.destroy(vb_);
    if (shader_.valid())
        device.destroy(shader_);
    ib_ = {};
    vb_ = {};
    shader_ = {};
    verts_.clear();
    device_ = nullptr;
}

void SpriteBatch::begin(rhi::IDevice& device, uint32_t screen_w, uint32_t screen_h) {
    begin(device, glm::ortho(0.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), 0.0f,
                             -1.0f, 1.0f));
}

void SpriteBatch::begin(rhi::IDevice& device, const mat4& projection) {
    assert(!in_frame_ && "SpriteBatch::begin called twice without end");
    device_ = &device;
    proj_ = projection;
    current_tex_ = {};
    verts_.clear();
    draw_calls_ = 0;
    quads_ = 0;
    in_frame_ = true;
}

void SpriteBatch::end() {
    assert(in_frame_ && "SpriteBatch::end without begin");
    flush();
    last_draw_calls_ = draw_calls_;
    last_quads_ = quads_;
    in_frame_ = false;
}

void SpriteBatch::draw(rhi::TextureHandle tex, vec2 pos, vec2 size, vec4 color) {
    draw(tex, pos, size, vec4(0.0f, 0.0f, 1.0f, 1.0f), color);
}

void SpriteBatch::draw(rhi::TextureHandle tex, vec2 pos, vec2 size, vec4 uv_rect, vec4 color) {
    assert(in_frame_ && "SpriteBatch::draw outside begin/end");
    if (!tex.valid())
        return;

    // Flush on texture change or when the per-batch cap is hit.
    if (current_tex_.id != tex.id || verts_.size() / 4 >= max_quads_) {
        flush();
        current_tex_ = tex;
    } else if (!current_tex_.valid()) {
        current_tex_ = tex;
    }

    const float hw = size.x * 0.5f;
    const float hh = size.y * 0.5f;
    const float x0 = pos.x - hw, x1 = pos.x + hw;
    const float y0 = pos.y - hh, y1 = pos.y + hh;
    const float u0 = uv_rect.x, v0 = uv_rect.y;
    const float u1 = uv_rect.z, v1 = uv_rect.w;
    // v0 is the top edge: textures upload row 0 first, so v = 0 is the image's
    // top row, and the screen-space ortho is y-down, so the smaller v belongs on
    // the smaller y. Pairing them the other way flips every sprite vertically.
    verts_.push_back({x0, y0, u0, v0, color.r, color.g, color.b, color.a});
    verts_.push_back({x1, y0, u1, v0, color.r, color.g, color.b, color.a});
    verts_.push_back({x1, y1, u1, v1, color.r, color.g, color.b, color.a});
    verts_.push_back({x0, y1, u0, v1, color.r, color.g, color.b, color.a});
    ++quads_;
}

void SpriteBatch::flush() {
    if (verts_.empty() || !current_tex_.valid())
        return;
    const auto byte_count = verts_.size() * sizeof(Vertex);
    device_->update_buffer(vb_, std::span<const std::byte>(
                                    reinterpret_cast<const std::byte*>(verts_.data()), byte_count));
    const uint32_t index_count = static_cast<uint32_t>(verts_.size() / 4) * 6;
    device_->draw_sprite_batch(shader_, vb_, ib_, index_count, current_tex_, proj_);
    ++draw_calls_;
    verts_.clear();
}

} // namespace vaxelis
