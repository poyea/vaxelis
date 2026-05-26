#include "engine/script/Script.hpp"

#include <cstdint>
#include <string>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "engine/core/Log.hpp"
#include "engine/input/Input.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

namespace vaxelis {

struct ScriptHost::Impl {
    sol::state lua;
    Scene*     scene{nullptr};
    Input*     input{nullptr};
    uint64_t   next_instance_id{1};
};

ScriptHost::ScriptHost()  : impl_(std::make_unique<Impl>()) {}
ScriptHost::~ScriptHost() = default;

sol::state& ScriptHost::lua() { return impl_->lua; }

bool ScriptHost::init(Scene& scene, Input& input) {
    impl_->scene = &scene;
    impl_->input = &input;

    auto& L = impl_->lua;
    L.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                     sol::lib::table, sol::lib::os);

    // vec2 type. Read/write fields, plus a constructor and a few helpers.
    L.new_usertype<vec2>(
        "vec2",
        sol::constructors<vec2(), vec2(float, float)>(),
        "x", &vec2::x,
        "y", &vec2::y);

    // engine.* namespace.
    auto engine = L["engine"].get_or_create<sol::table>();

    // engine.log(message) — info-level.
    engine.set_function("log", [](const std::string& msg) {
        VX_INFO("[lua] {}", msg);
    });

    // engine.input.down/pressed/released(action)
    auto inp = engine["input"].get_or_create<sol::table>();
    inp.set_function("down",     [this](const std::string& a) { return impl_->input->down(a); });
    inp.set_function("pressed",  [this](const std::string& a) { return impl_->input->pressed(a); });
    inp.set_function("released", [this](const std::string& a) { return impl_->input->released(a); });

    // engine.scene.find_by_name(name) -> entity id (uint32_t, 0 if missing)
    auto sc = engine["scene"].get_or_create<sol::table>();
    sc.set_function("find_by_name", [this](const std::string& name) -> uint32_t {
        entt::entity found = entt::null;
        impl_->scene->for_each([&](entt::entity e) {
            if (found != entt::null) return;
            if (impl_->scene->registry().get<Name>(e).value == name) found = e;
        });
        return static_cast<uint32_t>(found);
    });
    sc.set_function("get_position", [this](uint32_t id) -> vec2 {
        auto e = static_cast<entt::entity>(id);
        if (!impl_->scene->registry().valid(e)) return vec2{0.0f};
        return impl_->scene->registry().get<Transform2D>(e).position;
    });
    sc.set_function("set_position", [this](uint32_t id, vec2 p) {
        auto e = static_cast<entt::entity>(id);
        if (!impl_->scene->registry().valid(e)) return;
        impl_->scene->registry().get<Transform2D>(e).position = p;
    });
    sc.set_function("translate", [this](uint32_t id, vec2 d) {
        auto e = static_cast<entt::entity>(id);
        if (!impl_->scene->registry().valid(e)) return;
        impl_->scene->registry().get<Transform2D>(e).position += d;
    });
    return true;
}

void ScriptHost::update(float dt, Scene& scene) {
    auto& reg = scene.registry();
    auto view = reg.view<ScriptComponent>();
    for (auto e : view) {
        auto& sc = view.get<ScriptComponent>(e);
        if (sc.path.empty()) continue;

        if (!sc.loaded) {
            if (sc.instance_key.empty()) {
                sc.instance_key = "script_" + std::to_string(impl_->next_instance_id++);
            }
            // Create the per-instance subtable, expose entity_id, then run the
            // script with that table as its `self`/environment-lite.
            impl_->lua[sc.instance_key] = impl_->lua.create_table_with(
                "entity_id", static_cast<uint32_t>(e));
            sol::protected_function_result r = impl_->lua.safe_script_file(
                sc.path, sol::script_pass_on_error);
            if (!r.valid()) {
                sol::error err = r;
                VX_ERROR("Lua: failed to load {}: {}", sc.path, err.what());
                sc.loaded = true;  // don't retry on every frame
                continue;
            }
            // If the script returned a table, merge its callbacks into the
            // instance table so `on_update` is reachable.
            if (r.return_count() > 0 && r.get_type() == sol::type::table) {
                sol::table t = r;
                sol::table inst = impl_->lua[sc.instance_key];
                for (auto& kv : t) inst[kv.first] = kv.second;
            }
            // Call on_init if present.
            sol::table inst = impl_->lua[sc.instance_key];
            if (inst["on_init"].valid()) {
                sol::protected_function f = inst["on_init"];
                auto rr = f(inst);
                if (!rr.valid()) {
                    sol::error err = rr;
                    VX_ERROR("Lua: on_init in {}: {}", sc.path, err.what());
                }
            }
            sc.loaded = true;
        }

        sol::table inst = impl_->lua[sc.instance_key];
        if (!inst.valid()) continue;
        sol::object upd = inst["on_update"];
        if (upd.is<sol::protected_function>()) {
            sol::protected_function f = upd;
            auto rr = f(inst, dt);
            if (!rr.valid()) {
                sol::error err = rr;
                VX_ERROR("Lua: on_update in {}: {}", sc.path, err.what());
            }
        }
    }
}

}  // namespace vaxelis
