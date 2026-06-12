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
