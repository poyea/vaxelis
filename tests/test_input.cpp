// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <gtest/gtest.h>

#include <SDL3/SDL_events.h>

#include "engine/input/Input.hpp"

using namespace vaxelis;

namespace {
SDL_Event key_event(Uint32 type, SDL_Scancode sc) {
    SDL_Event ev{};
    ev.type = type;
    ev.key.scancode = sc;
    return ev;
}
} // namespace

TEST(Input, DownPressedReleasedEdges) {
    Input in;
    in.bind_action("jump", SDL_SCANCODE_SPACE);

    // Frame 1: press SPACE.
    in.begin_frame();
    auto down = key_event(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE);
    in.on_event(down);
    EXPECT_TRUE(in.down("jump"));
    EXPECT_TRUE(in.pressed("jump"));
    EXPECT_FALSE(in.released("jump"));

    // Frame 2: still held (no new events).
    in.begin_frame();
    EXPECT_TRUE(in.down("jump"));
    EXPECT_FALSE(in.pressed("jump"));
    EXPECT_FALSE(in.released("jump"));

    // Frame 3: release.
    in.begin_frame();
    auto up = key_event(SDL_EVENT_KEY_UP, SDL_SCANCODE_SPACE);
    in.on_event(up);
    EXPECT_FALSE(in.down("jump"));
    EXPECT_TRUE(in.released("jump"));
}

TEST(Input, MultiKeyActionSatisfiedByAnyBinding) {
    Input in;
    in.bind_action("left", {SDL_SCANCODE_A, SDL_SCANCODE_LEFT});

    in.begin_frame();
    auto down = key_event(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LEFT);
    in.on_event(down);
    EXPECT_TRUE(in.down("left"));
    EXPECT_TRUE(in.pressed("left"));
}

TEST(Input, UnknownActionReturnsFalse) {
    Input in;
    in.begin_frame();
    EXPECT_FALSE(in.down("nope"));
    EXPECT_FALSE(in.pressed("nope"));
    EXPECT_FALSE(in.released("nope"));
}

// A frame runs 0..N fixed steps, so frame-scoped edges are wrong inside one:
// above 60Hz most frames run none and a press is cleared before any step sees
// it; a frame running two steps fires the same press twice.
TEST(Input, StepEdgesSurviveAFrameThatRunsNoFixedStep) {
    Input in;
    in.bind_action("jump", SDL_SCANCODE_SPACE);

    // Frame 1: the key goes down, but no fixed step runs.
    in.begin_frame();
    in.on_event(key_event(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE));
    in.latch_edges();
    EXPECT_TRUE(in.pressed("jump")); // frame-scoped query still sees it

    // Frame 2: key still held, so the frame edge is gone...
    in.begin_frame();
    in.latch_edges();
    EXPECT_FALSE(in.pressed("jump"));

    // ...but the latch kept it, so the first step to run still acts on it.
    in.begin_fixed_step();
    EXPECT_TRUE(in.step_pressed("jump"));
}

TEST(Input, OnePressIsSeenByExactlyOneFixedStep) {
    Input in;
    in.bind_action("jump", SDL_SCANCODE_SPACE);

    in.begin_frame();
    in.on_event(key_event(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE));
    in.latch_edges();

    // A slow frame runs several steps; only the first may act on the press,
    // or the player jumps twice from one keystroke.
    in.begin_fixed_step();
    EXPECT_TRUE(in.step_pressed("jump"));
    in.begin_fixed_step();
    EXPECT_FALSE(in.step_pressed("jump"));
    in.begin_fixed_step();
    EXPECT_FALSE(in.step_pressed("jump"));
}

TEST(Input, StepReleaseEdgesLatchTheSameWay) {
    Input in;
    in.bind_action("jump", SDL_SCANCODE_SPACE);

    in.begin_frame();
    in.on_event(key_event(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE));
    in.latch_edges();
    in.begin_fixed_step(); // consume the press

    in.begin_frame();
    in.on_event(key_event(SDL_EVENT_KEY_UP, SDL_SCANCODE_SPACE));
    in.latch_edges();

    in.begin_fixed_step();
    EXPECT_TRUE(in.step_released("jump"));
    in.begin_fixed_step();
    EXPECT_FALSE(in.step_released("jump"));
}
