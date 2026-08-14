// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/text/Font.hpp"

#include <algorithm>

#include "engine/renderer/SpriteRenderer.hpp"

namespace vaxelis {

const Glyph* BakedFont::find(uint32_t codepoint) const {
    if (codepoint < first_codepoint)
        return nullptr;
    const size_t index = codepoint - first_codepoint;
    return index < glyphs.size() ? &glyphs[index] : nullptr;
}

namespace text {

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

void draw(SpriteBatch& batch, rhi::TextureHandle atlas, const BakedFont& font,
          std::string_view str, vec2 pos, vec4 color) {
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

} // namespace vaxelis
