// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// Baked bitmap fonts and string layout.

#include <cstddef>
#include <cstdint>
#include <span>
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

/// Bakes `ttf` (the raw bytes of a font file) into one square atlas at
/// `pixel_height`, covering `count` codepoints from `first_codepoint`.
/// Returns an invalid BakedFont when the data is not a font, or when the
/// glyphs do not fit the atlas -- raise `atlas_size` if so.
BakedFont bake(std::span<const std::byte> ttf, float pixel_height, uint32_t first_codepoint = 32,
               uint32_t count = 95, uint32_t atlas_size = 512);

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

/// A baked font that owns its GPU atlas.
///
/// Fonts are not assets the engine ships: point load() at whatever .ttf the
/// game supplies. Everything except the upload lives in BakedFont, so layout
/// can be exercised without a device.
class Font {
  public:
    /// Reads `path`, bakes it at `pixel_height` and uploads the atlas.
    /// @return false when the file is missing or the bake failed, both logged.
    bool load(rhi::IDevice& device, std::string_view path, float pixel_height);

    /// Releases the atlas texture.
    void shutdown(rhi::IDevice& device);

    /// True once a font is loaded and its atlas is on the GPU.
    bool valid() const { return m_baked.valid() && m_atlas.valid(); }

    /// The glyph table and metrics, for measure().
    const BakedFont& baked() const { return m_baked; }
    /// The atlas texture every glyph samples.
    rhi::TextureHandle atlas() const { return m_atlas; }
    /// Baseline-to-baseline distance for one line.
    float line_height() const { return m_baked.line_height; }

  private:
    BakedFont m_baked;
    rhi::TextureHandle m_atlas{};
};

namespace text {

/// Measures against a loaded Font. See measure(const BakedFont&, ...).
inline vec2 measure(const Font& font, std::string_view str) {
    return measure(font.baked(), str);
}

/// Draws through a loaded Font. See the BakedFont overload.
inline void draw(SpriteBatch& batch, const Font& font, std::string_view str, vec2 pos,
                 vec4 color = vec4(1.0f)) {
    draw(batch, font.atlas(), font.baked(), str, pos, color);
}

} // namespace text

} // namespace vaxelis
