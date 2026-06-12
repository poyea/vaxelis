#include <gtest/gtest.h>

#include "engine/physics/Components.hpp"
#include "engine/physics/Physics2D.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

using namespace vaxelis;

TEST(Physics2D, DynamicBodyFallsUnderGravity) {
    Physics2D phys;
    ASSERT_TRUE(phys.init({.gravity = {0.0f, 980.0f}, .pixels_per_meter = 100.0f, .sub_steps = 4}));

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
    EXPECT_GT(t.position.y, 50.0f);

    phys.shutdown();
}

TEST(Physics2D, DestroyingEntityReleasesTheBox2DBody) {
    Physics2D phys;
    ASSERT_TRUE(phys.init({}));

    Scene s;
    phys.register_with(s);

    auto e = s.create_node("Box");
    s.registry().emplace<RigidBody2D>(e);
    s.registry().emplace<BoxCollider2D>(e).half_extents = {8.0f, 8.0f};
    phys.sync_to_scene(s);

    const auto body_id = s.registry().get<RigidBody2D>(e).body;
    ASSERT_FALSE(B2_IS_NULL(body_id));
    ASSERT_TRUE(b2Body_IsValid(body_id));

    s.destroy_node(e);
    // After destroy, the body must be gone from Box2D's perspective even
    // though we still hold the id locally.
    EXPECT_FALSE(b2Body_IsValid(body_id));

    phys.shutdown();
}

TEST(Physics2D, SyncFromScenePushesExternalTransform2DEditsToTheBody) {
    Physics2D phys;
    ASSERT_TRUE(phys.init({.gravity = {0.0f, 0.0f}, .pixels_per_meter = 100.0f, .sub_steps = 4}));

    Scene s;
    auto e = s.create_node("Mover");
    s.registry().emplace<RigidBody2D>(e).type = BodyType::Kinematic;
    s.registry().emplace<BoxCollider2D>(e).half_extents = {8.0f, 8.0f};

    // Body is created at the origin on first sync.
    phys.sync_to_scene(s);
    const auto body = s.registry().get<RigidBody2D>(e).body;
    ASSERT_FALSE(B2_IS_NULL(body));

    // Move the entity externally, then push scene -> body.
    s.registry().get<Transform2D>(e).position = {300.0f, 150.0f};
    phys.sync_from_scene(s);

    const b2Vec2 p = b2Body_GetPosition(body);
    EXPECT_FLOAT_EQ(p.x, 3.0f); // 300px / 100ppm
    EXPECT_FLOAT_EQ(p.y, 1.5f); // 150px / 100ppm

    phys.shutdown();
}

TEST(Physics2D, FullTwoWayLoopStillLetsADynamicBodyFall) {
    Physics2D phys;
    ASSERT_TRUE(phys.init({.gravity = {0.0f, 980.0f}, .pixels_per_meter = 100.0f, .sub_steps = 4}));

    Scene s;
    auto e = s.create_node("Box");
    s.registry().emplace<RigidBody2D>(e); // dynamic
    s.registry().emplace<BoxCollider2D>(e).half_extents = {16.0f, 16.0f};

    phys.sync_to_scene(s); // create body
    for (int i = 0; i < 30; ++i) {
        phys.sync_from_scene(s); // no external edits -> must not pin the body
        phys.step(1.0f / 60.0f);
        phys.sync_to_scene(s);
    }
    EXPECT_GT(s.registry().get<Transform2D>(e).position.y, 50.0f);

    phys.shutdown();
}

TEST(Physics2D, StaticBodyDoesNotMove) {
    Physics2D phys;
    ASSERT_TRUE(phys.init({}));

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
    EXPECT_FLOAT_EQ(t.position.x, 100.0f);
    EXPECT_FLOAT_EQ(t.position.y, 500.0f);

    phys.shutdown();
}
