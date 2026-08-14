// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <gtest/gtest.h>

#include "engine/scene/Animation.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

using namespace vaxelis;

namespace {

/// A scene with one animated sprite. fps 4 means a quarter second per frame,
/// and both values are exact in binary floating point.
entt::entity animated(Scene& scene, AnimationMode mode, uint32_t frames = 3) {
    const entt::entity e = scene.create_node("Sprite");
    scene.registry().emplace<SpriteComponent>(e);
    auto& a = scene.registry().emplace<AnimationComponent>(e);
    a.frames = animation_frames(frames, 1);
    a.fps = 4.0f;
    a.mode = mode;
    return e;
}

uint32_t frame_of(Scene& scene, entt::entity e) {
    return scene.registry().get<AnimationComponent>(e).frame;
}

} // namespace

TEST(Animation, FramesSpanTheGridInReadingOrder) {
    const auto frames = animation_frames(2, 2);
    ASSERT_EQ(frames.size(), 4u);
    // Top-left cell of a 2x2 sheet.
    EXPECT_FLOAT_EQ(frames[0].x, 0.0f);
    EXPECT_FLOAT_EQ(frames[0].y, 0.0f);
    EXPECT_FLOAT_EQ(frames[0].z, 0.5f);
    EXPECT_FLOAT_EQ(frames[0].w, 0.5f);
    // Index 1 moves along the row, index 2 wraps to the next one.
    EXPECT_FLOAT_EQ(frames[1].x, 0.5f);
    EXPECT_FLOAT_EQ(frames[2].y, 0.5f);
    EXPECT_FLOAT_EQ(frames[3].z, 1.0f);
}

TEST(Animation, FrameCountCanStopShortOfTheGrid) {
    // A 4x2 sheet whose last two cells are blank.
    EXPECT_EQ(animation_frames(4, 2, 6).size(), 6u);
    // Asking for more than the grid holds is clamped, not extrapolated.
    EXPECT_EQ(animation_frames(2, 2, 99).size(), 4u);
    EXPECT_TRUE(animation_frames(0, 4).empty());
}

TEST(Animation, TheCurrentFrameIsWrittenIntoTheSprite) {
    Scene s;
    const entt::entity e = animated(s, AnimationMode::Loop);

    update_animations(s, 0.25f);
    EXPECT_EQ(frame_of(s, e), 1u);
    // The system's whole job: SpriteComponent::uv_rect follows the frame.
    const vec4 uv = s.registry().get<SpriteComponent>(e).uv_rect;
    EXPECT_FLOAT_EQ(uv.x, 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(uv.z, 2.0f / 3.0f);
}

TEST(Animation, LoopWrapsPastTheLastFrame) {
    Scene s;
    const entt::entity e = animated(s, AnimationMode::Loop);

    update_animations(s, 0.25f); // 1
    update_animations(s, 0.25f); // 2
    EXPECT_EQ(frame_of(s, e), 2u);
    update_animations(s, 0.25f); // back to 0
    EXPECT_EQ(frame_of(s, e), 0u);
}

TEST(Animation, OnceHoldsTheLastFrameAndReportsFinished) {
    Scene s;
    const entt::entity e = animated(s, AnimationMode::Once);

    update_animations(s, 0.25f);
    update_animations(s, 0.25f);
    EXPECT_EQ(frame_of(s, e), 2u);
    EXPECT_TRUE(s.registry().get<AnimationComponent>(e).playing);

    update_animations(s, 0.25f); // reaches the end
    EXPECT_EQ(frame_of(s, e), 2u);
    EXPECT_FALSE(s.registry().get<AnimationComponent>(e).playing);
    EXPECT_TRUE(s.registry().get<AnimationComponent>(e).finished());

    update_animations(s, 10.0f); // and stays there
    EXPECT_EQ(frame_of(s, e), 2u);
}

TEST(Animation, PingPongWalksBackDown) {
    Scene s;
    const entt::entity e = animated(s, AnimationMode::PingPong);

    update_animations(s, 0.25f);
    EXPECT_EQ(frame_of(s, e), 1u);
    update_animations(s, 0.25f);
    EXPECT_EQ(frame_of(s, e), 2u);
    update_animations(s, 0.25f); // turns around rather than wrapping
    EXPECT_EQ(frame_of(s, e), 1u);
    update_animations(s, 0.25f);
    EXPECT_EQ(frame_of(s, e), 0u);
    update_animations(s, 0.25f); // and back up again
    EXPECT_EQ(frame_of(s, e), 1u);
}

TEST(Animation, ALongDeltaAdvancesEveryFrameItCovers) {
    Scene s;
    const entt::entity e = animated(s, AnimationMode::Loop);
    // Three frames' worth in one step lands on frame 0 again, having passed
    // through 1 and 2 rather than jumping straight there.
    update_animations(s, 0.75f);
    EXPECT_EQ(frame_of(s, e), 0u);

    update_animations(s, 0.5f);
    EXPECT_EQ(frame_of(s, e), 2u);
}

TEST(Animation, PausedOrZeroFpsFreezesTheFrame) {
    Scene s;
    const entt::entity paused = animated(s, AnimationMode::Loop);
    s.registry().get<AnimationComponent>(paused).playing = false;

    const entt::entity frozen = animated(s, AnimationMode::Loop);
    s.registry().get<AnimationComponent>(frozen).fps = 0.0f;

    update_animations(s, 10.0f);
    EXPECT_EQ(frame_of(s, paused), 0u);
    EXPECT_EQ(frame_of(s, frozen), 0u);
}

TEST(Animation, EntitiesWithoutASpriteOrFramesAreSkipped) {
    Scene s;
    // Animation but no SpriteComponent: nothing to write to.
    const entt::entity no_sprite = s.create_node("Bare");
    s.registry().emplace<AnimationComponent>(no_sprite).frames = animation_frames(2, 1);

    // Sprite and animation, but no frames at all.
    const entt::entity no_frames = s.create_node("Empty");
    s.registry().emplace<SpriteComponent>(no_frames);
    s.registry().emplace<AnimationComponent>(no_frames);

    update_animations(s, 1.0f); // must not crash or advance anything
    EXPECT_EQ(frame_of(s, no_sprite), 0u);
    EXPECT_EQ(frame_of(s, no_frames), 0u);
}
