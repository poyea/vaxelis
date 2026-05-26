#pragma once

#include <box2d/box2d.h>

#include "engine/math/Math.hpp"

namespace vaxelis {

enum class BodyType { Static, Kinematic, Dynamic };

// Authoring component for a rigid body. `body` is filled in lazily on first
// physics step — it's the runtime handle and should not be serialized.
struct RigidBody2D {
    BodyType type{BodyType::Dynamic};
    float    linear_damping{0.0f};
    float    angular_damping{0.0f};
    bool     fixed_rotation{false};
    float    gravity_scale{1.0f};

    b2BodyId body{b2_nullBodyId};
};

// Box-shaped collider attached to the parent entity's body. Size in pixels;
// physics converts to meters internally.
struct BoxCollider2D {
    vec2  half_extents{16.0f, 16.0f};
    vec2  offset{0.0f, 0.0f};
    float density{1.0f};
    float friction{0.4f};
    float restitution{0.0f};
    bool  is_sensor{false};

    b2ShapeId shape{b2_nullShapeId};
};

}  // namespace vaxelis
