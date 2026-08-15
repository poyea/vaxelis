// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <gtest/gtest.h>

#include "RecordingDevice.hpp"
#include "engine/renderer/SpriteRenderer.hpp"

using namespace vaxelis;
using vaxelis::testing::RecordingDevice;

TEST(RenderTarget, HandlesAreNullByDefaultAndDistinctOnceCreated) {
    RecordingDevice dev;
    EXPECT_FALSE(rhi::RenderTargetHandle{}.valid());

    const auto a =
        dev.create_render_target({.width = 64, .height = 64}).value_or(rhi::RenderTargetHandle{});
    const auto b =
        dev.create_render_target({.width = 64, .height = 64}).value_or(rhi::RenderTargetHandle{});
    EXPECT_TRUE(a.valid());
    EXPECT_TRUE(b.valid());
    EXPECT_NE(a.id, b.id);
}

TEST(RenderTarget, TheColourAttachmentIsAnOrdinaryTexture) {
    RecordingDevice dev;
    const auto rt =
        dev.create_render_target({.width = 32, .height = 32}).value_or(rhi::RenderTargetHandle{});

    const rhi::TextureHandle color = dev.render_target_texture(rt);
    EXPECT_TRUE(color.valid());
    // Stable across calls: the target owns one texture, it does not mint more.
    EXPECT_EQ(dev.render_target_texture(rt).id, color.id);

    // Unknown handles read back null rather than asserting.
    EXPECT_FALSE(dev.render_target_texture(rhi::RenderTargetHandle{}).valid());
    EXPECT_FALSE(dev.render_target_texture(rhi::RenderTargetHandle{9999}).valid());
}

TEST(RenderTarget, DestroyingATargetReleasesItsTexture) {
    RecordingDevice dev;
    const auto rt =
        dev.create_render_target({.width = 32, .height = 32}).value_or(rhi::RenderTargetHandle{});
    ASSERT_TRUE(dev.render_target_texture(rt).valid());

    dev.destroy(rt);
    // The texture belonged to the target, so it goes with it.
    EXPECT_FALSE(dev.render_target_texture(rt).valid());
}

TEST(RenderTarget, BindingTracksTheCurrentTargetAndReturnsToTheBackbuffer) {
    RecordingDevice dev;
    const auto rt =
        dev.create_render_target({.width = 32, .height = 32}).value_or(rhi::RenderTargetHandle{});
    EXPECT_FALSE(dev.bound_target.valid()); // backbuffer to begin with

    dev.set_render_target(rt, vec4(0.0f));
    EXPECT_EQ(dev.bound_target.id, rt.id);

    // A null handle means the backbuffer; that is the documented way back.
    dev.set_render_target({}, vec4(0.0f));
    EXPECT_FALSE(dev.bound_target.valid());
    EXPECT_EQ(dev.target_binds, 2);
}

TEST(RenderTarget, DrawingIntoATargetGoesThroughTheSameBatcher) {
    RecordingDevice dev;
    SpriteBatch batch;
    ASSERT_TRUE(batch.init(dev));
    const auto rt =
        dev.create_render_target({.width = 64, .height = 64}).value_or(rhi::RenderTargetHandle{});
    const auto tex = dev.create_texture({}).value_or(rhi::TextureHandle{});

    // Offscreen pass.
    dev.set_render_target(rt, vec4(0.0f));
    batch.begin(dev, 64, 64);
    batch.draw(tex, {32.0f, 32.0f}, {16.0f, 16.0f});
    batch.end();
    EXPECT_EQ(dev.draw_calls, 1);

    // Back to the backbuffer, then sample what was just rendered.
    dev.set_render_target({}, vec4(0.0f));
    batch.begin(dev, 128, 128);
    batch.draw(dev.render_target_texture(rt), {64.0f, 64.0f}, {128.0f, 128.0f});
    batch.end();

    EXPECT_EQ(dev.draw_calls, 2);
    EXPECT_FALSE(dev.bound_target.valid());

    batch.shutdown(dev);
    dev.destroy(rt);
}
