// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <utility>

#include <gtest/gtest.h>

#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

using namespace vaxelis;

TEST(WorldTransform, UpdateWorldTransformsComposesParentChainIntoCache) {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    auto c = s.create_node("C", b);
    s.get<Transform2D>(a).position = {10.0f, 0.0f};
    s.get<Transform2D>(b).position = {5.0f, 0.0f};
    s.get<Transform2D>(c).position = {2.0f, 3.0f};

    s.update_world_transforms();

    const auto& wc = s.get<WorldTransform2D>(c).matrix;
    EXPECT_FLOAT_EQ(wc[3].x, 17.0f);
    EXPECT_FLOAT_EQ(wc[3].y, 3.0f);
}

TEST(WorldTransform, CachedWorldMatrixMatchesRecursiveFallback) {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    s.get<Transform2D>(a).position = {7.0f, -1.0f};
    s.get<Transform2D>(b).position = {3.0f, 4.0f};

    // Without an explicit update, world_matrix walks parents; result must
    // match the cached path after update_world_transforms runs.
    const auto walked = s.world_matrix(b);
    s.update_world_transforms();
    const auto cached = s.world_matrix(b);
    EXPECT_FLOAT_EQ(walked[3].x, cached[3].x);
    EXPECT_FLOAT_EQ(walked[3].y, cached[3].y);
}

// The traversal order is cached between calls, so every structural edit has to
// invalidate it. These pin the three that can.

TEST(WorldTransform, NodeCreatedAfterAnUpdateIsPickedUp) {
    Scene s;
    auto a = s.create_node("A");
    s.get<Transform2D>(a).position = {4.0f, 0.0f};
    s.update_world_transforms();

    auto b = s.create_node("B", a);
    s.get<Transform2D>(b).position = {1.0f, 2.0f};
    s.update_world_transforms();

    const auto& wb = s.get<WorldTransform2D>(b).matrix;
    EXPECT_FLOAT_EQ(wb[3].x, 5.0f);
    EXPECT_FLOAT_EQ(wb[3].y, 2.0f);
}

TEST(WorldTransform, ReparentAfterAnUpdateRecomposesTheChain) {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B");
    auto c = s.create_node("C", a);
    s.get<Transform2D>(a).position = {10.0f, 0.0f};
    s.get<Transform2D>(b).position = {0.0f, 20.0f};
    s.get<Transform2D>(c).position = {1.0f, 1.0f};
    s.update_world_transforms();

    s.set_parent(c, b);
    s.update_world_transforms();

    const auto& wc = s.get<WorldTransform2D>(c).matrix;
    EXPECT_FLOAT_EQ(wc[3].x, 1.0f);
    EXPECT_FLOAT_EQ(wc[3].y, 21.0f);
}

TEST(WorldTransform, RegistryDestroyInvalidatesTheCachedOrder) {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    s.get<Transform2D>(a).position = {10.0f, 0.0f};
    s.get<Transform2D>(b).position = {5.0f, 0.0f};
    s.update_world_transforms();

    // Bypasses destroy_node, so only the Hierarchy destroy signal can save the
    // cached order from naming a dead entity.
    s.registry().destroy(b);
    s.update_world_transforms();

    EXPECT_FALSE(s.valid(b));
    const auto& wa = s.get<WorldTransform2D>(a).matrix;
    EXPECT_FLOAT_EQ(wa[3].x, 10.0f);
}

TEST(WorldTransform, MovedSceneKeepsItsInvalidationListeners) {
    Scene src;
    auto a = src.create_node("A");
    src.get<Transform2D>(a).position = {2.0f, 0.0f};
    src.update_world_transforms();

    Scene dst(std::move(src));
    dst.update_world_transforms();

    // The listeners moved with the registry; if they still pointed at `src`
    // this node would never reach dst's traversal order.
    auto b = dst.create_node("B", a);
    dst.get<Transform2D>(b).position = {3.0f, 4.0f};
    dst.update_world_transforms();

    const auto& wb = dst.get<WorldTransform2D>(b).matrix;
    EXPECT_FLOAT_EQ(wb[3].x, 5.0f);
    EXPECT_FLOAT_EQ(wb[3].y, 4.0f);
}

TEST(WorldTransform, MovedFromSceneIsStillUsable) {
    Scene src;
    src.create_node("A");
    Scene dst(std::move(src));

    // Reset to a fresh scene rather than left hollow: the runtime clears a
    // scene with `m_scene = Scene{}`, which moves from the temporary.
    EXPECT_TRUE(src.valid(src.root()));
    auto fresh = src.create_node("Fresh");
    src.get<Transform2D>(fresh).position = {6.0f, 7.0f};
    src.update_world_transforms();

    const auto& wf = src.get<WorldTransform2D>(fresh).matrix;
    EXPECT_FLOAT_EQ(wf[3].x, 6.0f);
    EXPECT_FLOAT_EQ(wf[3].y, 7.0f);
}

TEST(WorldTransform, UpdateAfterDestroyLeavesSurvivorsIntact) {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    auto keep = s.create_node("Keep");
    s.get<Transform2D>(a).position = {10.0f, 0.0f};
    s.get<Transform2D>(b).position = {5.0f, 0.0f};
    s.get<Transform2D>(keep).position = {-3.0f, 8.0f};
    s.update_world_transforms();

    s.destroy_node(a);
    s.update_world_transforms();

    EXPECT_FALSE(s.valid(b));
    const auto& wk = s.get<WorldTransform2D>(keep).matrix;
    EXPECT_FLOAT_EQ(wk[3].x, -3.0f);
    EXPECT_FLOAT_EQ(wk[3].y, 8.0f);
}
