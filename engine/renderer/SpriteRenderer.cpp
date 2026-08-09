// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

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
    m_max_quads = max_quads;
    m_verts.reserve(static_cast<size_t>(m_max_quads) * 4);

    auto sh = device.create_shader({kVertexSrc, kFragmentSrc});
    if (!sh) {
        VX_ERROR("SpriteBatch: shader create failed ({})", rhi::to_string(sh.error()));
        return false;
    }
    m_shader = *sh;

    auto vb = device.create_buffer({rhi::BufferUsage::Vertex, rhi::BufferAccess::Dynamic,
                                    sizeof(Vertex) * m_max_quads * 4, nullptr});
    if (!vb) {
        device.destroy(m_shader);
        return false;
    }
    m_vb = *vb;

    // Pre-build the index buffer: each quad is 6 indices (two triangles) in a
    // fixed pattern, so it can live in a static buffer for the lifetime of the
    // batcher.
    std::vector<uint16_t> indices(static_cast<size_t>(m_max_quads) * 6);
    for (uint32_t q = 0; q < m_max_quads; ++q) {
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
        device.destroy(m_shader);
        device.destroy(m_vb);
        return false;
    }
    m_ib = *ib;
    return true;
}

void SpriteBatch::shutdown(rhi::IDevice& device) {
    if (m_ib.valid())
        device.destroy(m_ib);
    if (m_vb.valid())
        device.destroy(m_vb);
    if (m_shader.valid())
        device.destroy(m_shader);
    m_ib = {};
    m_vb = {};
    m_shader = {};
    m_verts.clear();
    m_device = nullptr;
}

void SpriteBatch::begin(rhi::IDevice& device, uint32_t screen_w, uint32_t screen_h) {
    begin(device, glm::ortho(0.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), 0.0f,
                             -1.0f, 1.0f));
}

void SpriteBatch::begin(rhi::IDevice& device, const mat4& projection) {
    assert(!m_in_frame && "SpriteBatch::begin called twice without end");
    m_device = &device;
    m_proj = projection;
    m_current_tex = {};
    m_verts.clear();
    m_draw_calls = 0;
    m_quads = 0;
    m_in_frame = true;
}

void SpriteBatch::end() {
    assert(m_in_frame && "SpriteBatch::end without begin");
    flush();
    m_last_draw_calls = m_draw_calls;
    m_last_quads = m_quads;
    m_in_frame = false;
}

void SpriteBatch::draw(rhi::TextureHandle tex, vec2 pos, vec2 size, vec4 color) {
    draw(tex, pos, size, vec4(0.0f, 0.0f, 1.0f, 1.0f), color);
}

void SpriteBatch::draw(rhi::TextureHandle tex, vec2 pos, vec2 size, vec4 uv_rect, vec4 color) {
    assert(m_in_frame && "SpriteBatch::draw outside begin/end");
    if (!tex.valid())
        return;

    // Flush on texture change or when the per-batch cap is hit.
    if (m_current_tex.id != tex.id || m_verts.size() / 4 >= m_max_quads) {
        flush();
        m_current_tex = tex;
    } else if (!m_current_tex.valid()) {
        m_current_tex = tex;
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
    m_verts.push_back({x0, y0, u0, v0, color.r, color.g, color.b, color.a});
    m_verts.push_back({x1, y0, u1, v0, color.r, color.g, color.b, color.a});
    m_verts.push_back({x1, y1, u1, v1, color.r, color.g, color.b, color.a});
    m_verts.push_back({x0, y1, u0, v1, color.r, color.g, color.b, color.a});
    ++m_quads;
}

void SpriteBatch::flush() {
    if (m_verts.empty() || !m_current_tex.valid())
        return;
    const auto byte_count = m_verts.size() * sizeof(Vertex);
    const auto* bytes = reinterpret_cast<const std::byte*>(m_verts.data());
    m_device->update_buffer(m_vb, std::span<const std::byte>(bytes, byte_count));
    const uint32_t index_count = static_cast<uint32_t>(m_verts.size() / 4) * 6;
    m_device->draw_sprite_batch(m_shader, m_vb, m_ib, index_count, m_current_tex, m_proj);
    ++m_draw_calls;
    m_verts.clear();
}

} // namespace vaxelis
