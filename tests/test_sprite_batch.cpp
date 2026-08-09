// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "engine/renderer/SpriteRenderer.hpp"
#include "engine/rhi/Rhi.hpp"

using namespace vaxelis;

namespace {

/// Mirrors the vertex layout Rhi.hpp documents for draw_sprite_batch:
/// interleaved position, uv and colour at a 32-byte stride.
struct Vertex {
    float x{0.0f};
    float y{0.0f};
    float u{0.0f};
    float v{0.0f};
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{0.0f};
};
static_assert(sizeof(Vertex) == 32, "must match the RHI's documented sprite vertex layout");

/// A device that keeps what the batcher submits instead of talking to a GPU, so
/// the vertices SpriteBatch builds can be inspected on the CPU.
class RecordingDevice final : public rhi::IDevice {
  public:
    std::vector<Vertex> vertices; ///< contents of the most recent update_buffer
    int draw_calls{0};

    expected<rhi::TextureHandle, rhi::RhiError> create_texture(const rhi::TextureDesc&) override {
        return rhi::TextureHandle{++m_next_id};
    }
    expected<rhi::ShaderHandle, rhi::RhiError> create_shader(const rhi::ShaderDesc&) override {
        return rhi::ShaderHandle{++m_next_id};
    }
    expected<rhi::BufferHandle, rhi::RhiError> create_buffer(const rhi::BufferDesc&) override {
        return rhi::BufferHandle{++m_next_id};
    }

    void destroy(rhi::TextureHandle) override {}
    void destroy(rhi::ShaderHandle) override {}
    void destroy(rhi::BufferHandle) override {}

    void update_buffer(rhi::BufferHandle, std::span<const std::byte> data, size_t) override {
        vertices.resize(data.size() / sizeof(Vertex));
        if (!data.empty())
            std::memcpy(vertices.data(), data.data(), data.size());
    }
    void update_texture(rhi::TextureHandle, const rhi::TextureUpdate&) override {}

    void begin_frame(vec4, uint32_t, uint32_t) override {}
    void end_frame() override {}

    void draw_sprite_batch(rhi::ShaderHandle, rhi::BufferHandle, rhi::BufferHandle, uint32_t,
                           rhi::TextureHandle, const mat4&) override {
        ++draw_calls;
    }

  private:
    uint32_t m_next_id{0};
};

/// Corner lookup by exact position; quad corners are computed from halved
/// extents, so the coordinates below are exact in binary floating point.
const Vertex& vertex_at(const std::vector<Vertex>& verts, float x, float y) {
    for (const Vertex& v : verts) {
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
    const Vertex& top_left = vertex_at(dev.vertices, 40.0f, 40.0f);
    const Vertex& bottom_right = vertex_at(dev.vertices, 60.0f, 60.0f);
    EXPECT_FLOAT_EQ(top_left.u, 0.25f);
    EXPECT_FLOAT_EQ(top_left.v, 0.0f);
    EXPECT_FLOAT_EQ(bottom_right.u, 0.75f);
    EXPECT_FLOAT_EQ(bottom_right.v, 0.5f);

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
    for (const Vertex& v : dev.vertices) {
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
