#pragma once

#include <memory>
#include <string>
#include <string_view>

// Forward-declare sol::state to keep sol's heavy headers out of clients.
namespace sol { class state; }

namespace vaxelis {

class Scene;
class Input;

// Owns the Lua VM, exposes engine bindings to scripts, runs per-script
// `on_update(dt)` callbacks.
//
// Scripts attach to scene entities via the ScriptComponent (path to a .lua
// file). Each script runs in its own Lua subtable namespace so globals don't
// collide; the table exposes `entity_id` plus any callbacks the script defines.
class ScriptHost {
public:
    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost&)            = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Binds engine API (vec2, scene helpers, input queries) into the Lua state.
    bool init(Scene& scene, Input& input);

    // Loads/reloads scripts for every ScriptComponent that needs it, then
    // calls on_update(dt) on each.
    void update(float dt, Scene& scene);

    sol::state& lua();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Per-entity component. `path` is the .lua file; `loaded` tracks whether the
// host has bound it. `instance_key` is the Lua subtable name (auto-assigned).
struct ScriptComponent {
    std::string path;
    bool        loaded{false};
    std::string instance_key;
};

}  // namespace vaxelis
