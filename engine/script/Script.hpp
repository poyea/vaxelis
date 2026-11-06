// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "engine/ecs/World.hpp"
#include "engine/scene/NodeId.hpp"

// Forward-declare sol::state to keep sol's heavy headers out of clients.
namespace sol {
class state;
}

namespace vaxelis {

class Scene;
class Input;

/// Owns the Lua VM, exposes engine bindings to scripts, runs per-script
/// `on_update(dt)` callbacks.
///
/// Scripts attach to scene entities via the ScriptComponent (path to a .lua
/// file). Each script runs in its own Lua subtable namespace so globals don't
/// collide; the table exposes `entity_id` plus any callbacks the script defines.
class ScriptHost {
  public:
    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;
    /// Not movable either: the destroy-signal listener names this exact object.
    ScriptHost(ScriptHost&&) = delete;
    ScriptHost& operator=(ScriptHost&&) = delete;

    /// Binds engine API (vec2, scene helpers, input queries) into the Lua state.
    bool init(Scene& scene, Input& input);

    /// Hooks a world destroy-signal so that removing a ScriptComponent (or
    /// destroying the owning entity) tears down the script's Lua subtable,
    /// letting the table and anything it captured be garbage-collected. Call
    /// once per Scene, after `init()`.
    ///
    /// The subscription names this object, so it must not outlive it: the
    /// destructor unsubscribes, and registering with a second scene
    /// unsubscribes from the first.
    void register_with(Scene& scene);

    /// Drops the subscription made by register_with(). Safe to call when none
    /// was made.
    void unregister();

    /// Loads/reloads scripts for every ScriptComponent that needs it, then
    /// calls on_update(dt) on each.
    void update(float dt, Scene& scene);

    /// Direct access to the underlying Lua state.
    sol::state& lua();

    /// True while the host still holds a live Lua subtable for `instance_key`.
    bool has_instance(const std::string& instance_key) const;
    /// Number of live script instances.
    size_t instance_count() const;

  private:
    void on_script_destroyed(ecs::World& world, NodeId e);

    /// The world register_with() subscribed to, so the destructor can
    /// unsubscribe. Null when nothing is subscribed.
    ecs::World* m_scene_world{nullptr};
    /// Expires if that world is destroyed first. Checked before the pointer
    /// above is dereferenced; see ecs::World::alive_token().
    std::weak_ptr<const void> m_world_alive;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Per-entity component. `path` is the .lua file; `loaded` tracks whether the
/// host has bound it. `instance_key` is the Lua subtable name (auto-assigned).
struct ScriptComponent {
    std::string path;
    bool loaded{false};
    std::string instance_key;
};

} // namespace vaxelis
