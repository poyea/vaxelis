#pragma once

#include <cstdint>

#include <box2d/box2d.h>
#include <entt/entt.hpp>

#include "engine/math/Math.hpp"

namespace vaxelis {

class Scene;

/// Box2D v3 world wrapper. World units = meters; converts to/from pixels via
/// `pixels_per_meter`. Step at a fixed dt (driven by Application's accumulator).
class Physics2D {
  public:
    /// World creation parameters.
    struct Config {
        vec2 gravity{0.0f, 980.0f}; ///< pixels/sec^2 (down = +Y, matches screen-space)
        float pixels_per_meter{100.0f};
        int sub_steps{4};
    };

    /// Creates the Box2D world with a default Config.
    bool init();
    /// Creates the Box2D world. Returns false if world creation fails.
    bool init(const Config& cfg);
    /// Destroys the world; all body/shape handles become invalid.
    void shutdown();
    /// True while a valid world exists.
    bool ready() const { return b2World_IsValid(m_world); }

    /// Hooks entt destroy-signals so that removing RigidBody2D / BoxCollider2D
    /// (or destroying the owning entity) releases the underlying Box2D handles.
    /// Call once per Scene, after `init()`. Connections detach when the
    /// registry is destroyed.
    void register_with(Scene& scene);

    /// Advances the world. dt is real seconds. After step(), call sync_to_scene()
    /// to push body positions back into the scene's Transform2D components.
    void step(float dt);

    /// Walks the scene, lazily creating bodies for RigidBody2D + BoxCollider2D
    /// pairs, and writes transforms back from physics bodies to Transform2D.
    void sync_to_scene(Scene& scene);

    /// Pushes externally-edited Transform2D values into Box2D (Transform2D ->
    /// body). Call BEFORE step(), pairing with sync_to_scene() after. Only
    /// bodies whose Transform2D changed since the last sync are pushed, so the
    /// simulation stays authoritative for untouched dynamic bodies; a moved
    /// dynamic body is also woken so it doesn't sleep through the change.
    void sync_from_scene(Scene& scene);

    float pixels_per_meter() const { return m_ppm; }
    b2WorldId world() const { return m_world; }

  private:
    void on_rb_destroyed(entt::registry& reg, entt::entity e);
    void on_col_destroyed(entt::registry& reg, entt::entity e);

    b2WorldId m_world{b2_nullWorldId};
    Config m_cfg{};
    float m_ppm{100.0f};
    float m_inv_ppm{0.01f};
};

} // namespace vaxelis
