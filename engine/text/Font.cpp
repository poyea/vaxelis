// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/text/Font.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "engine/core/Log.hpp"
#include "engine/renderer/SpriteRenderer.hpp"

namespace vaxelis {

const Glyph* BakedFont::find(uint32_t codepoint) const {
    if (codepoint < first_codepoint)
        return nullptr;
    const size_t index = codepoint - first_codepoint;
    return index < glyphs.size() ? &glyphs[index] : nullptr;
}

namespace text {

BakedFont bake(std::span<const std::byte> ttf, float pixel_height, uint32_t first_codepoint,
               uint32_t count, uint32_t atlas_size) {
    BakedFont out;
    if (ttf.empty() || pixel_height <= 0.0f || count == 0 || atlas_size == 0)
        return out;

    const auto* data = reinterpret_cast<const unsigned char*>(ttf.data());

    // Line spacing comes from the font's own vertical metrics rather than the
    // requested pixel height, which is only the em size.
    stbtt_fontinfo info;
    if (stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0)) == 0) {
        VX_ERROR("Font: not a usable font file");
        return out;
    }
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    const float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);

    std::vector<unsigned char> coverage(static_cast<size_t>(atlas_size) * atlas_size, 0);
    std::vector<stbtt_bakedchar> baked(count);
    unsigned char* bmp = coverage.data();
    stbtt_bakedchar* chars = baked.data();
    const int edge = static_cast<int>(atlas_size);
    const int first = static_cast<int>(first_codepoint);
    const int n = static_cast<int>(count);
    const int rows = stbtt_BakeFontBitmap(data, 0, pixel_height, bmp, edge, edge, first, n, chars);
    if (rows <= 0) {
        VX_ERROR("Font: {} glyphs at {}px do not fit a {}x{} atlas", count, pixel_height,
                 atlas_size, atlas_size);
        return out;
    }

    // The bake produces coverage only. Expand to white RGBA with coverage in
    // alpha, so a glyph takes its colour from the vertex colour it is drawn
    // with rather than being baked one colour.
    out.pixels.resize(coverage.size());
    for (size_t i = 0; i < coverage.size(); ++i)
        out.pixels[i] = 0x00FFFFFFu | (static_cast<uint32_t>(coverage[i]) << 24);

    const float inv = 1.0f / static_cast<float>(atlas_size);
    out.glyphs.reserve(count);
    for (const stbtt_bakedchar& b : baked) {
        const float u0 = static_cast<float>(b.x0) * inv;
        const float v0 = static_cast<float>(b.y0) * inv;
        const float u1 = static_cast<float>(b.x1) * inv;
        const float v1 = static_cast<float>(b.y1) * inv;

        Glyph g;
        g.uv = {u0, v0, u1, v1};
        g.size = {static_cast<float>(b.x1 - b.x0), static_cast<float>(b.y1 - b.y0)};
        g.offset = {b.xoff, b.yoff};
        g.advance = b.xadvance;
        out.glyphs.push_back(g);
    }

    out.width = atlas_size;
    out.height = atlas_size;
    out.first_codepoint = first_codepoint;
    out.line_height = static_cast<float>(ascent - descent + line_gap) * scale;
    return out;
}

vec2 measure(const BakedFont& font, std::string_view str) {
    if (!font.valid())
        return {0.0f, 0.0f};

    float widest = 0.0f;
    float line = 0.0f;
    int lines = 1;
    for (const char c : str) {
        if (c == '\n') {
            widest = std::max(widest, line);
            line = 0.0f;
            ++lines;
            continue;
        }
        if (const Glyph* g = font.find(static_cast<unsigned char>(c)))
            line += g->advance;
    }
    widest = std::max(widest, line);
    return {widest, static_cast<float>(lines) * font.line_height};
}

void draw(SpriteBatch& batch, rhi::TextureHandle atlas, const BakedFont& font, std::string_view str,
          vec2 pos, vec4 color) {
    if (!font.valid() || !atlas.valid())
        return;

    vec2 pen = pos;
    for (const char c : str) {
        if (c == '\n') {
            pen.x = pos.x;
            pen.y += font.line_height;
            continue;
        }
        const Glyph* g = font.find(static_cast<unsigned char>(c));
        if (g == nullptr)
            continue;
        // A space carries an advance but no quad; skipping the draw keeps it
        // out of the batch instead of submitting an empty one.
        if (g->size.x > 0.0f && g->size.y > 0.0f) {
            // SpriteBatch places quads by their centre, the glyph by its
            // top-left corner.
            const vec2 centre = pen + g->offset + g->size * 0.5f;
            batch.draw(atlas, centre, g->size, g->uv, color);
        }
        pen.x += g->advance;
    }
}

} // namespace text

bool Font::load(rhi::IDevice& device, std::string_view path, float pixel_height) {
    std::ifstream file{std::string(path), std::ios::binary};
    if (!file) {
        VX_ERROR("Font: cannot open {}", path);
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string bytes = buffer.str();

    const auto* first = reinterpret_cast<const std::byte*>(bytes.data());
    m_baked = text::bake({first, bytes.size()}, pixel_height);
    if (!m_baked.valid())
        return false;

    rhi::TextureDesc td{
        .width = m_baked.width,
        .height = m_baked.height,
        .format = rhi::TextureFormat::RGBA8,
        .initial_data = m_baked.pixels.data(),
    };
    const auto tex = device.create_texture(td);
    if (!tex) {
        VX_ERROR("Font: atlas upload failed for {}", path);
        m_baked = {};
        return false;
    }
    m_atlas = *tex;
    return true;
}

void Font::shutdown(rhi::IDevice& device) {
    if (m_atlas.valid())
        device.destroy(m_atlas);
    m_atlas = {};
    m_baked = {};
}

} // namespace vaxelis
