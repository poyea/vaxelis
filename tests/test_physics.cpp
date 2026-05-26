#include <catch2/catch_test_macros.hpp>

#include "engine/physics/Components.hpp"
#include "engine/physics/Physics2D.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

using namespace vaxelis;

TEST_CASE("Physics2D: dynamic body falls under gravity") {
    Physics2D phys;
    REQUIRE(phys.init({.gravity = {0.0f, 980.0f}, .pixels_per_meter = 100.0f, .sub_steps = 4}));

    Scene s;
    auto e = s.create_node("Box");
    s.registry().get<Transform2D>(e).position = {0.0f, 0.0f};
    // Dynamic body by default.
    s.registry().emplace<RigidBody2D>(e);
    s.registry().emplace<BoxCollider2D>(e).half_extents = {16.0f, 16.0f};

    // Create body + simulate ~0.5s in fixed steps.
    phys.sync_to_scene(s);
    for (int i = 0; i < 30; ++i) {
        phys.step(1.0f / 60.0f);
        phys.sync_to_scene(s);
    }
    const auto& t = s.registry().get<Transform2D>(e);
    // Should have fallen well past start.
    REQUIRE(t.position.y > 50.0f);

    phys.shutdown();
}

TEST_CASE("Physics2D: static body does not move") {
    Physics2D phys;
    REQUIRE(phys.init({}));

    Scene s;
    auto e = s.create_node("Ground");
    s.registry().get<Transform2D>(e).position = {100.0f, 500.0f};
    s.registry().emplace<RigidBody2D>(e).type = BodyType::Static;
    s.registry().emplace<BoxCollider2D>(e).half_extents = {200.0f, 16.0f};

    phys.sync_to_scene(s);
    for (int i = 0; i < 10; ++i) {
        phys.step(1.0f / 60.0f);
        phys.sync_to_scene(s);
    }
    const auto& t = s.registry().get<Transform2D>(e);
    REQUIRE(t.position.x == 100.0f);
    REQUIRE(t.position.y == 500.0f);

    phys.shutdown();
}
