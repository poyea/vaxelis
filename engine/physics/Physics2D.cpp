// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

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
    m_cfg = cfg;
    m_ppm = cfg.pixels_per_meter > 0.0f ? cfg.pixels_per_meter : 100.0f;
    m_inv_ppm = 1.0f / m_ppm;

    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = {cfg.gravity.x * m_inv_ppm, cfg.gravity.y * m_inv_ppm};
    m_world = b2CreateWorld(&wd);
    if (!b2World_IsValid(m_world)) {
        VX_ERROR("Physics2D: b2CreateWorld failed");
        return false;
    }
    VX_INFO("Physics2D: world created (ppm={}, sub_steps={})", m_ppm, cfg.sub_steps);
    return true;
}

void Physics2D::shutdown() {
    if (b2World_IsValid(m_world)) {
        b2DestroyWorld(m_world);
        m_world = b2_nullWorldId;
    }
    // The world owned every body and shape it handed out, so the ids left in
    // the components are now dangling. Clearing them matters for init() on the
    // same scene: sync_to_scene treats a non-null id as a live body, so it
    // would create nothing and then call b2Body_GetPosition on a dead handle.
    if (m_scene_world != nullptr) {
        m_scene_world->each<RigidBody2D>([](RigidBody2D& rb) { rb.body = b2_nullBodyId; });
        m_scene_world->each<BoxCollider2D>([](BoxCollider2D& col) { col.shape = b2_nullShapeId; });
    }
}

Physics2D::~Physics2D() {
    // shutdown() clears the component handles, which needs the subscription's
    // world pointer, so it runs before unregister() drops it. Neither fires a
    // destroy signal, so the listeners cannot be reached in between.
    shutdown();
    unregister();
}

void Physics2D::register_with(Scene& scene) {
    unregister();
    m_scene_world = &scene.world();
    m_scene_world->on_destroy<RigidBody2D>().connect<&Physics2D::on_rb_destroyed>(*this);
    m_scene_world->on_destroy<BoxCollider2D>().connect<&Physics2D::on_col_destroyed>(*this);
}

void Physics2D::unregister() {
    if (m_scene_world == nullptr)
        return;
    // By instance: drops both subscriptions whichever member each one names.
    m_scene_world->on_destroy<RigidBody2D>().disconnect(this);
    m_scene_world->on_destroy<BoxCollider2D>().disconnect(this);
    m_scene_world = nullptr;
}

void Physics2D::on_rb_destroyed(ecs::World& scene_world, NodeId e) {
    if (!b2World_IsValid(m_world))
        return;
    RigidBody2D* rb_slot = scene_world.try_get<RigidBody2D>(e);
    if (rb_slot == nullptr)
        return;
    RigidBody2D& rb = *rb_slot;
    if (B2_IS_NULL(rb.body))
        return;
    // Destroying the body also releases any attached shapes (Box2D v3
    // semantics), so we mark the collider's shape null to keep the C++ state
    // consistent before the BoxCollider2D destroy-signal fires.
    if (auto* col = scene_world.try_get<BoxCollider2D>(e))
        col->shape = b2_nullShapeId;
    b2DestroyBody(rb.body);
    rb.body = b2_nullBodyId;
}

void Physics2D::on_col_destroyed(ecs::World& scene_world, NodeId e) {
    if (!b2World_IsValid(m_world))
        return;
    BoxCollider2D* col_slot = scene_world.try_get<BoxCollider2D>(e);
    if (col_slot == nullptr)
        return;
    BoxCollider2D& col = *col_slot;
    if (B2_IS_NULL(col.shape))
        return;
    b2DestroyShape(col.shape, /*updateBodyMass=*/true);
    col.shape = b2_nullShapeId;
}

void Physics2D::step(float dt) {
    if (!b2World_IsValid(m_world))
        return;
    b2World_Step(m_world, dt, m_cfg.sub_steps);
}

void Physics2D::sync_to_scene(Scene& scene) {
    if (!b2World_IsValid(m_world))
        return;
    // 1) Create bodies for any RigidBody2D that doesn't have one yet, and
    //    attach BoxCollider2D shapes for any unbound collider on the same entity.
    scene.each_node<RigidBody2D, Transform2D>([&](NodeId e, RigidBody2D& rb, Transform2D& tr) {
        if (B2_IS_NULL(rb.body)) {
            b2BodyDef bd = b2DefaultBodyDef();
            bd.type = to_b2(rb.type);
            bd.position = {tr.position.x * m_inv_ppm, tr.position.y * m_inv_ppm};
            bd.rotation = b2MakeRot(tr.rotation);
            bd.linearDamping = rb.linear_damping;
            bd.angularDamping = rb.angular_damping;
            bd.motionLocks.angularZ = rb.fixed_rotation;
            bd.gravityScale = rb.gravity_scale;
            rb.body = b2CreateBody(m_world, &bd);
            rb.last_sync_position = tr.position;
            rb.last_sync_rotation = tr.rotation;
        }
        if (auto* col = scene.try_get<BoxCollider2D>(e)) {
            if (B2_IS_NULL(col->shape) && !B2_IS_NULL(rb.body)) {
                b2ShapeDef sd = b2DefaultShapeDef();
                sd.density = col->density;
                sd.material.friction = col->friction;
                sd.material.restitution = col->restitution;
                sd.isSensor = col->is_sensor;
                // Box2D wants half-extents in meters around a centered point.
                b2Polygon poly = b2MakeOffsetBox(
                    col->half_extents.x * m_inv_ppm, col->half_extents.y * m_inv_ppm,
                    {col->offset.x * m_inv_ppm, col->offset.y * m_inv_ppm}, b2Rot_identity);
                col->shape = b2CreatePolygonShape(rb.body, &sd, &poly);
            }
        }
    });

    // 2) Write physics-driven transforms back to the scene. Static bodies are
    //    skipped since they're authored, not simulated.
    scene.each<RigidBody2D, Transform2D>([&](RigidBody2D& rb, Transform2D& tr) {
        if (rb.type == BodyType::Static)
            return;
        if (B2_IS_NULL(rb.body))
            return;
        const b2Vec2 p = b2Body_GetPosition(rb.body);
        const b2Rot rt = b2Body_GetRotation(rb.body);
        tr.position = {p.x * m_ppm, p.y * m_ppm};
        tr.rotation = b2Rot_GetAngle(rt);
        rb.last_sync_position = tr.position;
        rb.last_sync_rotation = tr.rotation;
    });
}

void Physics2D::sync_from_scene(Scene& scene) {
    if (!b2World_IsValid(m_world))
        return;
    scene.each<RigidBody2D, Transform2D>([this](RigidBody2D& rb, Transform2D& tr) {
        if (B2_IS_NULL(rb.body))
            return;
        // Skip bodies that haven't moved since our last write; comparing
        // against the exact value we last stored makes this an equality test,
        // not a tolerance one, so the simulation owns untouched dynamic bodies.
        if (tr.position == rb.last_sync_position && tr.rotation == rb.last_sync_rotation)
            return;
        b2Body_SetTransform(rb.body, b2Vec2{tr.position.x * m_inv_ppm, tr.position.y * m_inv_ppm},
                            b2MakeRot(tr.rotation));
        if (rb.type == BodyType::Dynamic)
            b2Body_SetAwake(rb.body, true);
        rb.last_sync_position = tr.position;
        rb.last_sync_rotation = tr.rotation;
    });
}

} // namespace vaxelis
