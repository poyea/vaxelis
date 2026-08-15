// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "RecordingDevice.hpp"
#include "engine/renderer/SpriteRenderer.hpp"
#include "engine/text/Font.hpp"

using namespace vaxelis;
using vaxelis::testing::RecordedVertex;
using vaxelis::testing::RecordingDevice;

namespace {

/// Three glyphs starting at 'A', with deliberately distinct metrics: 'A' and
/// 'B' draw, 'C' stands in for a space (advance but no quad).
BakedFont make_font() {
    BakedFont f;
    f.width = 64;
    f.height = 64;
    f.first_codepoint = 'A';
    f.line_height = 20.0f;
    f.pixels.assign(64 * 64, 0xFFFFFFFFu);
    Glyph a;
    a.uv = {0.0f, 0.0f, 0.25f, 0.25f};
    a.size = {8.0f, 16.0f};
    a.offset = {1.0f, -12.0f};
    a.advance = 10.0f;

    Glyph b;
    b.uv = {0.25f, 0.0f, 0.5f, 0.25f};
    b.size = {6.0f, 16.0f};
    b.offset = {0.0f, -12.0f};
    b.advance = 7.0f;

    Glyph space;
    space.advance = 5.0f;

    f.glyphs = {a, b, space};
    return f;
}

} // namespace

TEST(Font, FindsOnlyBakedCodepoints) {
    const BakedFont f = make_font();
    ASSERT_NE(f.find('A'), nullptr);
    EXPECT_FLOAT_EQ(f.find('A')->advance, 10.0f);
    EXPECT_FLOAT_EQ(f.find('B')->advance, 7.0f);
    // Below the first codepoint and past the end both read back absent.
    EXPECT_EQ(f.find(' '), nullptr);
    EXPECT_EQ(f.find('Z'), nullptr);
}

TEST(Font, AnEmptyFontIsNotValid) {
    const BakedFont empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.find('A'), nullptr);
    EXPECT_FLOAT_EQ(text::measure(empty, "AAA").x, 0.0f);
}

TEST(Text, MeasureSumsAdvancesAndCountsLines) {
    const BakedFont f = make_font();

    EXPECT_FLOAT_EQ(text::measure(f, "").x, 0.0f);
    EXPECT_FLOAT_EQ(text::measure(f, "").y, 20.0f); // one empty line
    EXPECT_FLOAT_EQ(text::measure(f, "AB").x, 17.0f);
    EXPECT_FLOAT_EQ(text::measure(f, "AB").y, 20.0f);

    // Unbaked codepoints contribute nothing rather than a fallback width.
    EXPECT_FLOAT_EQ(text::measure(f, "AZB").x, 17.0f);
}

TEST(Text, MeasureTakesTheWidestLine) {
    const BakedFont f = make_font();
    // "AA" is 20 wide, "B" is 7; the box is the wider of them by two lines.
    const vec2 size = text::measure(f, "AA\nB");
    EXPECT_FLOAT_EQ(size.x, 20.0f);
    EXPECT_FLOAT_EQ(size.y, 40.0f);
}

TEST(Text, DrawPlacesGlyphsByTheirTopLeftCorner) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto atlas = dev.create_texture({}).value_or(rhi::TextureHandle{});
    const BakedFont f = make_font();

    batch.begin(dev, 200, 200);
    text::draw(batch, atlas, f, "A", {100.0f, 100.0f}, vec4(1.0f));
    batch.end();

    ASSERT_EQ(dev.vertices.size(), 4u);
    // pen(100,100) + offset(1,-12) is the top-left; the quad is 8x16 from there.
    float min_x = dev.vertices[0].x;
    float min_y = dev.vertices[0].y;
    for (const RecordedVertex& v : dev.vertices) {
        min_x = std::min(min_x, v.x);
        min_y = std::min(min_y, v.y);
    }
    EXPECT_FLOAT_EQ(min_x, 101.0f);
    EXPECT_FLOAT_EQ(min_y, 88.0f);

    batch.shutdown(dev);
}

TEST(Text, AWholeStringIsOneDrawCall) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto atlas = dev.create_texture({}).value_or(rhi::TextureHandle{});
    const BakedFont f = make_font();

    batch.begin(dev, 200, 200);
    text::draw(batch, atlas, f, "ABAB", {0.0f, 0.0f}, vec4(1.0f));
    batch.end();

    // Four glyphs, one atlas, so the batcher never has to flush mid-string.
    EXPECT_EQ(batch.quads(), 4u);
    EXPECT_EQ(dev.draw_calls, 1);

    batch.shutdown(dev);
}

TEST(Text, GlyphsWithoutAQuadAdvanceButDrawNothing) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto atlas = dev.create_texture({}).value_or(rhi::TextureHandle{});
    const BakedFont f = make_font();

    batch.begin(dev, 200, 200);
    text::draw(batch, atlas, f, "ACA", {0.0f, 0.0f}, vec4(1.0f)); // 'C' is the space
    batch.end();

    EXPECT_EQ(batch.quads(), 2u); // the space submitted no quad
    batch.shutdown(dev);
}

TEST(Text, NothingIsDrawnWithoutAFontOrAtlas) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto atlas = dev.create_texture({}).value_or(rhi::TextureHandle{});

    batch.begin(dev, 200, 200);
    text::draw(batch, atlas, BakedFont{}, "AB", {0.0f, 0.0f}, vec4(1.0f));
    text::draw(batch, rhi::TextureHandle{}, make_font(), "AB", {0.0f, 0.0f}, vec4(1.0f));
    batch.end();

    EXPECT_EQ(batch.quads(), 0u);
    EXPECT_EQ(dev.draw_calls, 0);
    batch.shutdown(dev);
}

TEST(Font, BakeRejectsInputThatIsNotAFont) {
    // No .ttf ships with the engine, so the bake path is covered by what it
    // must refuse rather than what it produces.
    EXPECT_FALSE(text::bake({}, 16.0f).valid());

    // Not a font: the format probe reports -1, which must not reach
    // stbtt_InitFont as a base-pointer offset.
    const std::vector<std::byte> garbage(2048, std::byte{0x7F});
    EXPECT_FALSE(text::bake(garbage, 16.0f).valid());

    // Too short for even the offset table to be read safely.
    const std::vector<std::byte> stub(4, std::byte{0x00});
    EXPECT_FALSE(text::bake(stub, 16.0f).valid());

    // Degenerate parameters are refused before stb ever sees them.
    EXPECT_FALSE(text::bake(garbage, 0.0f).valid());
    EXPECT_FALSE(text::bake(garbage, 16.0f, 32, 0).valid());
    EXPECT_FALSE(text::bake(garbage, 16.0f, 32, 95, 0).valid());
}
