#pragma once

#include <cstdint>

#include <box2d/box2d.h>

#include "engine/math/Math.hpp"

namespace vaxelis {

class Scene;

// Box2D v3 world wrapper. World units = meters; converts to/from pixels via
// `pixels_per_meter`. Step at a fixed dt (driven by Application's accumulator).
class Physics2D {
public:
    struct Config {
        vec2  gravity{0.0f, 980.0f};   // pixels/sec^2 (down = +Y, matches screen-space)
        float pixels_per_meter{100.0f};
        int   sub_steps{4};
    };

    bool init(const Config& cfg = {});
    void shutdown();
    bool ready() const { return b2World_IsValid(world_); }

    // Advances the world. dt is real seconds. After step(), call sync_to_scene()
    // to push body positions back into the scene's Transform2D components.
    void step(float dt);

    // Walks the scene, lazily creating bodies for RigidBody2D + BoxCollider2D
    // pairs, and writes transforms back from physics bodies to Transform2D.
    void sync_to_scene(Scene& scene);

    float pixels_per_meter() const { return ppm_; }
    b2WorldId world() const { return world_; }

private:
    b2WorldId world_{b2_nullWorldId};
    Config    cfg_{};
    float     ppm_{100.0f};
    float     inv_ppm_{0.01f};
};

}  // namespace vaxelis
