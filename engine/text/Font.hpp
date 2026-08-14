// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// Baked bitmap fonts and string layout.

#include <cstdint>
#include <string_view>
#include <vector>

#include "engine/math/Math.hpp"
#include "engine/rhi/Rhi.hpp"

namespace vaxelis {

class SpriteBatch;

/// One glyph's place in the atlas and how to position it against the pen.
struct Glyph {
    /// Atlas sub-rect as (min_u, min_v, max_u, max_v), matching SpriteBatch.
    vec4 uv{0.0f, 0.0f, 0.0f, 0.0f};
    /// Quad size in pixels.
    vec2 size{0.0f, 0.0f};
    /// Top-left of the quad relative to the pen; y is usually negative, since
    /// the pen sits on the baseline and most glyphs rise above it.
    vec2 offset{0.0f, 0.0f};
    /// How far the pen moves after drawing, including the glyph's side bearing.
    float advance{0.0f};
};

/// A font baked to a single atlas at one pixel height: the pixels plus the
/// glyph table, with no GPU involvement, so it can be built and measured on the
/// CPU. Font owns the uploaded version of this.
struct BakedFont {
    /// RGBA8, row-major. White throughout; coverage lives in the alpha channel,
    /// so a glyph takes its colour from the vertex colour it is drawn with.
    std::vector<uint32_t> pixels;
    uint32_t width{0};
    uint32_t height{0};
    /// Codepoint the glyph table starts at; entries below it are absent.
    uint32_t first_codepoint{32};
    std::vector<Glyph> glyphs;
    /// Baseline-to-baseline distance for one line.
    float line_height{0.0f};

    /// True once a bake produced a usable table.
    bool valid() const { return !glyphs.empty() && line_height > 0.0f; }

    /// The glyph for `codepoint`, or nullptr when it was not baked.
    const Glyph* find(uint32_t codepoint) const;
};

namespace text {

/// Size `str` would occupy if drawn: the widest line by the total line height.
/// Newlines start a new line; unbaked codepoints contribute nothing.
vec2 measure(const BakedFont& font, std::string_view str);

/// Draws `str` with the first line's top-left at `pos`, in `color`.
///
/// Caller controls batch begin/end, as with tiled::render. Every glyph comes
/// from one atlas, so a whole string costs one draw call as long as nothing
/// else changes texture in between.
void draw(SpriteBatch& batch, rhi::TextureHandle atlas, const BakedFont& font,
          std::string_view str, vec2 pos, vec4 color = vec4(1.0f));

} // namespace text

} // namespace vaxelis
