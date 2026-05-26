#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("Input: down/pressed/released edges") {
    Input in;
    in.bind_action("jump", SDL_SCANCODE_SPACE);

    // Frame 1: press SPACE.
    in.begin_frame();
    auto down = key_event(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE);
    in.on_event(down);
    REQUIRE(in.down("jump"));
    REQUIRE(in.pressed("jump"));
    REQUIRE_FALSE(in.released("jump"));

    // Frame 2: still held (no new events).
    in.begin_frame();
    REQUIRE(in.down("jump"));
    REQUIRE_FALSE(in.pressed("jump"));
    REQUIRE_FALSE(in.released("jump"));

    // Frame 3: release.
    in.begin_frame();
    auto up = key_event(SDL_EVENT_KEY_UP, SDL_SCANCODE_SPACE);
    in.on_event(up);
    REQUIRE_FALSE(in.down("jump"));
    REQUIRE(in.released("jump"));
}

TEST_CASE("Input: multi-key action satisfied by any binding") {
    Input in;
    in.bind_action("left", {SDL_SCANCODE_A, SDL_SCANCODE_LEFT});

    in.begin_frame();
    auto down = key_event(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LEFT);
    in.on_event(down);
    REQUIRE(in.down("left"));
    REQUIRE(in.pressed("left"));
}

TEST_CASE("Input: unknown action returns false") {
    Input in;
    in.begin_frame();
    REQUIRE_FALSE(in.down("nope"));
    REQUIRE_FALSE(in.pressed("nope"));
    REQUIRE_FALSE(in.released("nope"));
}
