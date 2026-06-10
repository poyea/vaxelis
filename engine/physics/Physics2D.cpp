#include "engine/physics/Physics2D.hpp"

#include "engine/core/Log.hpp"
#include "engine/physics/Components.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

namespace vaxelis {

namespace {

b2BodyType to_b2(BodyType t) {
    switch (t) {
    case BodyType::Static:
        return b2_staticBody;
    case BodyType::Kinematic:
        return b2_kinematicBody;
    case BodyType::Dynamic:
        return b2_dynamicBody;
    }
    return b2_dynamicBody;
}

} // namespace

bool Physics2D::init() {
    return init(Config{});
}

bool Physics2D::init(const Config& cfg) {
    cfg_ = cfg;
    ppm_ = cfg.pixels_per_meter > 0.0f ? cfg.pixels_per_meter : 100.0f;
    inv_ppm_ = 1.0f / ppm_;

    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = {cfg.gravity.x * inv_ppm_, cfg.gravity.y * inv_ppm_};
    world_ = b2CreateWorld(&wd);
    if (!b2World_IsValid(world_)) {
        VX_ERROR("Physics2D: b2CreateWorld failed");
        return false;
    }
    VX_INFO("Physics2D: world created (ppm={}, sub_steps={})", ppm_, cfg.sub_steps);
    return true;
}

void Physics2D::shutdown() {
    if (b2World_IsValid(world_)) {
        b2DestroyWorld(world_);
        world_ = b2_nullWorldId;
    }
}

void Physics2D::register_with(Scene& scene) {
    auto& reg = scene.registry();
    reg.on_destroy<RigidBody2D>().connect<&Physics2D::on_rb_destroyed>(*this);
    reg.on_destroy<BoxCollider2D>().connect<&Physics2D::on_col_destroyed>(*this);
}

void Physics2D::on_rb_destroyed(entt::registry& reg, entt::entity e) {
    if (!b2World_IsValid(world_))
        return;
    auto& rb = reg.get<RigidBody2D>(e);
    if (B2_IS_NULL(rb.body))
        return;
    // Destroying the body also releases any attached shapes (Box2D v3
    // semantics), so we mark the collider's shape null to keep the C++ state
    // consistent before the BoxCollider2D destroy-signal fires.
    if (auto* col = reg.try_get<BoxCollider2D>(e))
        col->shape = b2_nullShapeId;
    b2DestroyBody(rb.body);
    rb.body = b2_nullBodyId;
}

void Physics2D::on_col_destroyed(entt::registry& reg, entt::entity e) {
    if (!b2World_IsValid(world_))
        return;
    auto& col = reg.get<BoxCollider2D>(e);
    if (B2_IS_NULL(col.shape))
        return;
    b2DestroyShape(col.shape, /*updateBodyMass=*/true);
    col.shape = b2_nullShapeId;
}

void Physics2D::step(float dt) {
    if (!b2World_IsValid(world_))
        return;
    b2World_Step(world_, dt, cfg_.sub_steps);
}

void Physics2D::sync_to_scene(Scene& scene) {
    if (!b2World_IsValid(world_))
        return;
    auto& reg = scene.registry();

    // 1) Create bodies for any RigidBody2D that doesn't have one yet, and
    //    attach BoxCollider2D shapes for any unbound collider on the same entity.
    auto view = reg.view<RigidBody2D, Transform2D>();
    for (auto e : view) {
        auto& rb = view.get<RigidBody2D>(e);
        auto& tr = view.get<Transform2D>(e);
        if (B2_IS_NULL(rb.body)) {
            b2BodyDef bd = b2DefaultBodyDef();
            bd.type = to_b2(rb.type);
            bd.position = {tr.position.x * inv_ppm_, tr.position.y * inv_ppm_};
            bd.rotation = b2MakeRot(tr.rotation);
            bd.linearDamping = rb.linear_damping;
            bd.angularDamping = rb.angular_damping;
            bd.motionLocks.angularZ = rb.fixed_rotation;
            bd.gravityScale = rb.gravity_scale;
            rb.body = b2CreateBody(world_, &bd);
            rb.last_sync_position = tr.position;
            rb.last_sync_rotation = tr.rotation;
        }
        if (auto* col = reg.try_get<BoxCollider2D>(e)) {
            if (B2_IS_NULL(col->shape) && !B2_IS_NULL(rb.body)) {
                b2ShapeDef sd = b2DefaultShapeDef();
                sd.density = col->density;
                sd.material.friction = col->friction;
                sd.material.restitution = col->restitution;
                sd.isSensor = col->is_sensor;
                // Box2D wants half-extents in meters around a centered point.
                b2Polygon poly = b2MakeOffsetBox(
                    col->half_extents.x * inv_ppm_, col->half_extents.y * inv_ppm_,
                    {col->offset.x * inv_ppm_, col->offset.y * inv_ppm_}, b2Rot_identity);
                col->shape = b2CreatePolygonShape(rb.body, &sd, &poly);
            }
        }
    }

    // 2) Write physics-driven transforms back to the scene. Static bodies are
    //    skipped since they're authored, not simulated.
    for (auto e : view) {
        auto& rb = view.get<RigidBody2D>(e);
        auto& tr = view.get<Transform2D>(e);
        if (rb.type == BodyType::Static)
            continue;
        if (B2_IS_NULL(rb.body))
            continue;
        const b2Vec2 p = b2Body_GetPosition(rb.body);
        const b2Rot rt = b2Body_GetRotation(rb.body);
        tr.position = {p.x * ppm_, p.y * ppm_};
        tr.rotation = b2Rot_GetAngle(rt);
        rb.last_sync_position = tr.position;
        rb.last_sync_rotation = tr.rotation;
    }
}

void Physics2D::sync_from_scene(Scene& scene) {
    if (!b2World_IsValid(world_))
        return;
    auto& reg = scene.registry();
    auto view = reg.view<RigidBody2D, Transform2D>();
    for (auto e : view) {
        auto& rb = view.get<RigidBody2D>(e);
        auto& tr = view.get<Transform2D>(e);
        if (B2_IS_NULL(rb.body))
            continue;
        // Skip bodies that haven't moved since our last write; comparing
        // against the exact value we last stored makes this an equality test,
        // not a tolerance one, so the simulation owns untouched dynamic bodies.
        if (tr.position == rb.last_sync_position && tr.rotation == rb.last_sync_rotation)
            continue;
        b2Body_SetTransform(rb.body, b2Vec2{tr.position.x * inv_ppm_, tr.position.y * inv_ppm_},
                            b2MakeRot(tr.rotation));
        if (rb.type == BodyType::Dynamic)
            b2Body_SetAwake(rb.body, true);
        rb.last_sync_position = tr.position;
        rb.last_sync_rotation = tr.rotation;
    }
}

} // namespace vaxelis
