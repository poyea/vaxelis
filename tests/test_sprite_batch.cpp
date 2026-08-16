// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "RecordingDevice.hpp"
#include "engine/renderer/SpriteRenderer.hpp"

using namespace vaxelis;
using vaxelis::testing::RecordedVertex;
using vaxelis::testing::RecordingDevice;

namespace {

/// Corner lookup by exact position; quad corners are computed from halved
/// extents, so the coordinates below are exact in binary floating point.
const RecordedVertex& vertex_at(const std::vector<RecordedVertex>& verts, float x, float y) {
    for (const RecordedVertex& v : verts) {
        if (v.x == x && v.y == y)
            return v;
    }
    ADD_FAILURE() << "no vertex at (" << x << ", " << y << ")";
    return verts.front();
}

} // namespace

// A texture uploads row 0 first, so v = 0 is the image's top row, and the
// screen-space ortho is y-down, so the top of the quad is its smallest y. The
// two have to meet: v = 0 belongs on the top edge, or every sprite drawn with
// the default uv rect comes out upside down.
TEST(SpriteBatch, DefaultUvRectPutsTextureRowZeroOnTheTopEdge) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto tex = dev.create_texture({}).value_or(rhi::TextureHandle{});
    ASSERT_TRUE(tex.valid());

    batch.begin(dev, 100, 100);
    batch.draw(tex, {50.0f, 50.0f}, {20.0f, 20.0f});
    batch.end();

    ASSERT_EQ(dev.vertices.size(), 4u);
    EXPECT_FLOAT_EQ(vertex_at(dev.vertices, 40.0f, 40.0f).v, 0.0f); // top-left
    EXPECT_FLOAT_EQ(vertex_at(dev.vertices, 60.0f, 40.0f).v, 0.0f); // top-right
    EXPECT_FLOAT_EQ(vertex_at(dev.vertices, 40.0f, 60.0f).v, 1.0f); // bottom-left
    EXPECT_FLOAT_EQ(vertex_at(dev.vertices, 60.0f, 60.0f).v, 1.0f); // bottom-right
    EXPECT_EQ(dev.draw_calls, 1);

    batch.shutdown(dev);
}

TEST(SpriteBatch, SubRectMapsMinUvToTheTopLeftCorner) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto tex = dev.create_texture({}).value_or(rhi::TextureHandle{});

    // uv_rect is (min_u, min_v, max_u, max_v): the top-left half of an atlas.
    batch.begin(dev, 100, 100);
    batch.draw(tex, {50.0f, 50.0f}, {20.0f, 20.0f}, vec4{0.25f, 0.0f, 0.75f, 0.5f}, vec4{1.0f});
    batch.end();

    ASSERT_EQ(dev.vertices.size(), 4u);
    const RecordedVertex& top_left = vertex_at(dev.vertices, 40.0f, 40.0f);
    const RecordedVertex& bottom_right = vertex_at(dev.vertices, 60.0f, 60.0f);
    EXPECT_FLOAT_EQ(top_left.u, 0.25f);
    EXPECT_FLOAT_EQ(top_left.v, 0.0f);
    EXPECT_FLOAT_EQ(bottom_right.u, 0.75f);
    EXPECT_FLOAT_EQ(bottom_right.v, 0.5f);

    batch.shutdown(dev);
}

// The matrix overload exists so a sprite is not stuck axis-aligned. Both cases
// below use exactly representable matrices, so the corners are exact.
TEST(SpriteBatch, TransformScaleWidensTheQuadAroundItsOrigin) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto tex = dev.create_texture({}).value_or(rhi::TextureHandle{});

    mat4 m(1.0f);
    m[0] = vec4(2.0f, 0.0f, 0.0f, 0.0f); // x scaled 2x
    m[1] = vec4(0.0f, 3.0f, 0.0f, 0.0f); // y scaled 3x
    m[3] = vec4(10.0f, 20.0f, 0.0f, 1.0f);

    batch.begin(dev, 100, 100);
    batch.draw(tex, m, {4.0f, 4.0f}, vec4{0.0f, 0.0f, 1.0f, 1.0f}, vec4{1.0f});
    batch.end();

    // A 4x4 quad becomes 8x12, still centered on the transform's origin.
    ASSERT_EQ(dev.vertices.size(), 4u);
    EXPECT_FLOAT_EQ(vertex_at(dev.vertices, 6.0f, 14.0f).u, 0.0f);  // top-left
    EXPECT_FLOAT_EQ(vertex_at(dev.vertices, 14.0f, 26.0f).u, 1.0f); // bottom-right
    EXPECT_FLOAT_EQ(vertex_at(dev.vertices, 14.0f, 26.0f).v, 1.0f);

    batch.shutdown(dev);
}

TEST(SpriteBatch, TransformRotationMovesTheCornersAndLeavesTheUvsPut) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto tex = dev.create_texture({}).value_or(rhi::TextureHandle{});

    // A quarter turn, written out rather than derived from cos/sin so the
    // expected corners stay exact.
    mat4 m(1.0f);
    m[0] = vec4(0.0f, 1.0f, 0.0f, 0.0f);
    m[1] = vec4(-1.0f, 0.0f, 0.0f, 0.0f);

    batch.begin(dev, 100, 100);
    batch.draw(tex, m, {4.0f, 2.0f}, vec4{0.0f, 0.0f, 1.0f, 1.0f}, vec4{1.0f});
    batch.end();

    // The 4-wide, 2-tall quad now stands 2 wide and 4 tall.
    ASSERT_EQ(dev.vertices.size(), 4u);
    // Rotation moves the corner but not which texel it samples: the quad's own
    // top-left still carries (min_u, min_v) wherever it lands.
    const RecordedVertex& local_top_left = vertex_at(dev.vertices, 1.0f, -2.0f);
    EXPECT_FLOAT_EQ(local_top_left.u, 0.0f);
    EXPECT_FLOAT_EQ(local_top_left.v, 0.0f);
    const RecordedVertex& local_bottom_right = vertex_at(dev.vertices, -1.0f, 2.0f);
    EXPECT_FLOAT_EQ(local_bottom_right.u, 1.0f);
    EXPECT_FLOAT_EQ(local_bottom_right.v, 1.0f);

    batch.shutdown(dev);
}

TEST(SpriteBatch, AnIdentityTransformMatchesThePositionOverload) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto tex = dev.create_texture({}).value_or(rhi::TextureHandle{});

    mat4 m(1.0f);
    m[3] = vec4(50.0f, 50.0f, 0.0f, 1.0f);
    batch.begin(dev, 100, 100);
    batch.draw(tex, m, {20.0f, 20.0f}, vec4{0.0f, 0.0f, 1.0f, 1.0f}, vec4{1.0f});
    batch.end();
    const std::vector<RecordedVertex> via_matrix = dev.vertices;

    dev.vertices.clear();
    batch.begin(dev, 100, 100);
    batch.draw(tex, {50.0f, 50.0f}, {20.0f, 20.0f});
    batch.end();

    ASSERT_EQ(via_matrix.size(), dev.vertices.size());
    for (size_t i = 0; i < via_matrix.size(); ++i) {
        EXPECT_FLOAT_EQ(via_matrix[i].x, dev.vertices[i].x);
        EXPECT_FLOAT_EQ(via_matrix[i].y, dev.vertices[i].y);
        EXPECT_FLOAT_EQ(via_matrix[i].u, dev.vertices[i].u);
        EXPECT_FLOAT_EQ(via_matrix[i].v, dev.vertices[i].v);
    }

    batch.shutdown(dev);
}

TEST(SpriteBatch, VertexColorIsCarriedToEveryCorner) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto tex = dev.create_texture({}).value_or(rhi::TextureHandle{});

    batch.begin(dev, 100, 100);
    batch.draw(tex, {50.0f, 50.0f}, {20.0f, 20.0f}, vec4{0.2f, 0.4f, 0.6f, 0.8f});
    batch.end();

    ASSERT_EQ(dev.vertices.size(), 4u);
    for (const RecordedVertex& v : dev.vertices) {
        EXPECT_FLOAT_EQ(v.r, 0.2f);
        EXPECT_FLOAT_EQ(v.g, 0.4f);
        EXPECT_FLOAT_EQ(v.b, 0.6f);
        EXPECT_FLOAT_EQ(v.a, 0.8f);
    }

    batch.shutdown(dev);
}

TEST(SpriteBatch, SameTextureCoalescesIntoOneDrawCall) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto tex = dev.create_texture({}).value_or(rhi::TextureHandle{});

    batch.begin(dev, 100, 100);
    batch.draw(tex, {10.0f, 10.0f}, {4.0f, 4.0f});
    batch.draw(tex, {20.0f, 20.0f}, {4.0f, 4.0f});
    batch.draw(tex, {30.0f, 30.0f}, {4.0f, 4.0f});
    batch.end();

    EXPECT_EQ(dev.draw_calls, 1);
    EXPECT_EQ(batch.quads(), 3u);

    batch.shutdown(dev);
}

TEST(SpriteBatch, TextureChangeSplitsTheBatch) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto first = dev.create_texture({}).value_or(rhi::TextureHandle{});
    const auto second = dev.create_texture({}).value_or(rhi::TextureHandle{});
    ASSERT_NE(first.id, second.id);

    batch.begin(dev, 100, 100);
    batch.draw(first, {10.0f, 10.0f}, {4.0f, 4.0f});
    batch.draw(second, {20.0f, 20.0f}, {4.0f, 4.0f});
    batch.end();

    EXPECT_EQ(dev.draw_calls, 2);
    EXPECT_EQ(batch.quads(), 2u);

    batch.shutdown(dev);
}

TEST(SpriteBatch, InvalidTextureIsSkipped) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));

    batch.begin(dev, 100, 100);
    batch.draw(rhi::TextureHandle{}, {10.0f, 10.0f}, {4.0f, 4.0f});
    batch.end();

    EXPECT_EQ(dev.draw_calls, 0);
    EXPECT_EQ(batch.quads(), 0u);

    batch.shutdown(dev);
}
