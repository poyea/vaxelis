#include <catch2/catch_test_macros.hpp>

#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

using namespace vaxelis;

TEST_CASE("Scene: update_world_transforms composes parent chain into cache") {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    auto c = s.create_node("C", b);
    s.registry().get<Transform2D>(a).position = {10.0f, 0.0f};
    s.registry().get<Transform2D>(b).position = {5.0f, 0.0f};
    s.registry().get<Transform2D>(c).position = {2.0f, 3.0f};

    s.update_world_transforms();

    const auto& wc = s.registry().get<WorldTransform2D>(c).matrix;
    REQUIRE(wc[3].x == 17.0f);
    REQUIRE(wc[3].y == 3.0f);
}

TEST_CASE("Scene: cached world_matrix matches recursive fallback") {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    s.registry().get<Transform2D>(a).position = {7.0f, -1.0f};
    s.registry().get<Transform2D>(b).position = {3.0f, 4.0f};

    // Without an explicit update, world_matrix walks parents; result must
    // match the cached path after update_world_transforms runs.
    const auto walked = s.world_matrix(b);
    s.update_world_transforms();
    const auto cached = s.world_matrix(b);
    REQUIRE(walked[3].x == cached[3].x);
    REQUIRE(walked[3].y == cached[3].y);
}
