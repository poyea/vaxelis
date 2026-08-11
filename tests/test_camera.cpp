// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <gtest/gtest.h>

#include "engine/scene/Camera2D.hpp"

using namespace vaxelis;

TEST(Camera2D, VisibleBoundsCoverTheFramebufferAtUnitZoom) {
    Camera2D cam;
    cam.position = {100.0f, 50.0f};

    const AABB2 v = cam.visible_bounds(200, 100);
    // Position is the centre, so the rect is half the framebuffer either side.
    EXPECT_FLOAT_EQ(v.min.x, 0.0f);
    EXPECT_FLOAT_EQ(v.min.y, 0.0f);
    EXPECT_FLOAT_EQ(v.max.x, 200.0f);
    EXPECT_FLOAT_EQ(v.max.y, 100.0f);
}

TEST(Camera2D, ZoomScalesTheVisibleRectInversely) {
    Camera2D cam;
    cam.position = {0.0f, 0.0f};

    cam.zoom = 2.0f; // magnifies, so less world is on screen
    AABB2 v = cam.visible_bounds(400, 200);
    EXPECT_FLOAT_EQ(v.min.x, -100.0f);
    EXPECT_FLOAT_EQ(v.max.x, 100.0f);
    EXPECT_FLOAT_EQ(v.min.y, -50.0f);
    EXPECT_FLOAT_EQ(v.max.y, 50.0f);

    cam.zoom = 0.5f; // zoomed out, so more of the world fits
    v = cam.visible_bounds(400, 200);
    EXPECT_FLOAT_EQ(v.min.x, -400.0f);
    EXPECT_FLOAT_EQ(v.max.x, 400.0f);
}

TEST(Camera2D, ProjectionMapsTheVisibleRectOntoNdc) {
    Camera2D cam;
    cam.position = {320.0f, 180.0f};
    cam.zoom = 1.5f;

    const AABB2 v = cam.visible_bounds(640, 360);
    const mat4 proj = cam.projection(640, 360);

    // The camera is y-down, so the rect's min corner is top-left and has to
    // land at NDC (-1, +1) -- not (-1, -1) as a y-up projection would give.
    const vec4 top_left = proj * vec4(v.min.x, v.min.y, 0.0f, 1.0f);
    EXPECT_NEAR(top_left.x, -1.0f, 1e-5f);
    EXPECT_NEAR(top_left.y, 1.0f, 1e-5f);

    const vec4 bottom_right = proj * vec4(v.max.x, v.max.y, 0.0f, 1.0f);
    EXPECT_NEAR(bottom_right.x, 1.0f, 1e-5f);
    EXPECT_NEAR(bottom_right.y, -1.0f, 1e-5f);

    // The camera position is the centre of the screen by definition.
    const vec4 centre = proj * vec4(cam.position, 0.0f, 1.0f);
    EXPECT_NEAR(centre.x, 0.0f, 1e-5f);
    EXPECT_NEAR(centre.y, 0.0f, 1e-5f);
}

TEST(Camera2D, ApplyBoundsIsANoOpWhenBoundsAreDegenerate) {
    Camera2D cam;
    cam.position = {-9999.0f, 9999.0f};
    // bounds_min == bounds_max, the "no clamping" signal.
    cam.apply_bounds(200, 200);
    EXPECT_FLOAT_EQ(cam.position.x, -9999.0f);
    EXPECT_FLOAT_EQ(cam.position.y, 9999.0f);
}

TEST(Camera2D, ApplyBoundsKeepsTheViewInsideTheWorld) {
    Camera2D cam;
    cam.bounds_min = {0.0f, 0.0f};
    cam.bounds_max = {1000.0f, 1000.0f};

    // Half extents are 100, so the centre cannot go nearer than 100 to an edge
    // without showing what lies outside the bounds.
    cam.position = {-500.0f, -500.0f};
    cam.apply_bounds(200, 200);
    EXPECT_FLOAT_EQ(cam.position.x, 100.0f);
    EXPECT_FLOAT_EQ(cam.position.y, 100.0f);

    cam.position = {5000.0f, 5000.0f};
    cam.apply_bounds(200, 200);
    EXPECT_FLOAT_EQ(cam.position.x, 900.0f);
    EXPECT_FLOAT_EQ(cam.position.y, 900.0f);

    // A position already inside the clamp window is left alone.
    cam.position = {400.0f, 600.0f};
    cam.apply_bounds(200, 200);
    EXPECT_FLOAT_EQ(cam.position.x, 400.0f);
    EXPECT_FLOAT_EQ(cam.position.y, 600.0f);
}

TEST(Camera2D, ApplyBoundsCentresAnAxisNarrowerThanTheView) {
    Camera2D cam;
    // The world is 100 wide but the view is 200, so there is no position that
    // keeps the view inside it; the axis centres on the bounds instead.
    cam.bounds_min = {0.0f, 0.0f};
    cam.bounds_max = {100.0f, 1000.0f};
    cam.position = {900.0f, 500.0f};

    cam.apply_bounds(200, 200);
    EXPECT_FLOAT_EQ(cam.position.x, 50.0f);  // centred
    EXPECT_FLOAT_EQ(cam.position.y, 500.0f); // still clamped normally
}

TEST(Camera2D, ApplyBoundsAccountsForZoom) {
    Camera2D cam;
    cam.bounds_min = {0.0f, 0.0f};
    cam.bounds_max = {1000.0f, 1000.0f};
    cam.zoom = 2.0f; // half extents become 50, so the camera may go closer in
    cam.position = {0.0f, 0.0f};

    cam.apply_bounds(200, 200);
    EXPECT_FLOAT_EQ(cam.position.x, 50.0f);
    EXPECT_FLOAT_EQ(cam.position.y, 50.0f);
}
