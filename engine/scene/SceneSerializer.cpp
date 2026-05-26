#include "engine/scene/SceneSerializer.hpp"

#include <fstream>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "engine/core/Log.hpp"
#include "engine/scene/Scene.hpp"

namespace vaxelis::scene_io {

namespace {

using json = nlohmann::json;

json save_vec2(const vec2& v) { return json::array({v.x, v.y}); }
json save_vec4(const vec4& v) { return json::array({v.x, v.y, v.z, v.w}); }
vec2 load_vec2(const json& j) {
    return vec2{j.at(0).get<float>(), j.at(1).get<float>()};
}
vec4 load_vec4(const json& j) {
    return vec4{j.at(0).get<float>(), j.at(1).get<float>(),
                j.at(2).get<float>(), j.at(3).get<float>()};
}

}  // namespace

std::string to_json(const Scene& s, int indent) {
    // Assign per-file ids: 0 = root, 1..N = real nodes in DFS order.
    std::unordered_map<entt::entity, int> ids;
    ids[s.root()] = 0;
    int next_id = 1;
    s.for_each([&](entt::entity e) {
        if (e == s.root()) return;
        ids[e] = next_id++;
    });

    json out;
    out["nodes"] = json::array();
    s.for_each([&](entt::entity e) {
        if (e == s.root()) return;
        const auto& reg = s.registry();
        json node;
        node["id"]     = ids[e];
        node["name"]   = reg.get<Name>(e).value;
        node["parent"] = ids[reg.get<Hierarchy>(e).parent];

        const auto& t = reg.get<Transform2D>(e);
        node["transform"] = {
            {"pos",   save_vec2(t.position)},
            {"rot",   t.rotation},
            {"scale", save_vec2(t.scale)},
        };

        if (const auto* sp = reg.try_get<SpriteComponent>(e)) {
            node["sprite"] = {
                {"texture_key", sp->texture_key},
                {"size",        save_vec2(sp->size)},
                {"uv_rect",     save_vec4(sp->uv_rect)},
                {"color",       save_vec4(sp->color)},
                {"z_order",     sp->z_order},
                {"visible",     sp->visible},
            };
        }
        out["nodes"].push_back(std::move(node));
    });
    return out.dump(indent);
}

bool from_json(Scene& s, std::string_view jtext) {
    json j;
    try {
        j = json::parse(jtext);
    } catch (const std::exception& e) {
        VX_ERROR("Scene load: JSON parse failed: {}", e.what());
        return false;
    }
    if (!j.contains("nodes") || !j["nodes"].is_array()) {
        VX_ERROR("Scene load: missing 'nodes' array");
        return false;
    }

    // Two-pass: create all entities (so parent ids resolve), then wire up
    // parents and populate components.
    std::unordered_map<int, entt::entity> by_file_id;
    by_file_id[0] = s.root();

    for (const auto& node : j["nodes"]) {
        int id = node.value("id", 0);
        if (id == 0) continue;
        auto e = s.create_node(node.value("name", std::string{"Node"}));
        by_file_id[id] = e;
    }

    for (const auto& node : j["nodes"]) {
        int id = node.value("id", 0);
        if (id == 0) continue;
        auto it = by_file_id.find(id);
        if (it == by_file_id.end()) continue;
        auto e = it->second;

        int parent_id = node.value("parent", 0);
        auto pit = by_file_id.find(parent_id);
        if (pit != by_file_id.end()) s.set_parent(e, pit->second);

        if (node.contains("transform")) {
            const auto& tj = node["transform"];
            auto& t = s.registry().get<Transform2D>(e);
            if (tj.contains("pos"))   t.position = load_vec2(tj["pos"]);
            if (tj.contains("rot"))   t.rotation = tj["rot"].get<float>();
            if (tj.contains("scale")) t.scale    = load_vec2(tj["scale"]);
        }
        if (node.contains("sprite")) {
            const auto& sj = node["sprite"];
            SpriteComponent sp;
            sp.texture_key = sj.value("texture_key", std::string{});
            if (sj.contains("size"))    sp.size    = load_vec2(sj["size"]);
            if (sj.contains("uv_rect")) sp.uv_rect = load_vec4(sj["uv_rect"]);
            if (sj.contains("color"))   sp.color   = load_vec4(sj["color"]);
            sp.z_order = sj.value("z_order", 0);
            sp.visible = sj.value("visible", true);
            s.registry().emplace<SpriteComponent>(e, std::move(sp));
        }
    }
    return true;
}

bool save_file(const Scene& s, std::string_view path) {
    std::ofstream f((std::string(path)));
    if (!f) { VX_ERROR("Scene save: cannot open {}", path); return false; }
    f << to_json(s);
    return f.good();
}

bool load_file(Scene& s, std::string_view path) {
    std::ifstream f((std::string(path)));
    if (!f) { VX_ERROR("Scene load: cannot open {}", path); return false; }
    std::stringstream ss; ss << f.rdbuf();
    return from_json(s, ss.str());
}

}  // namespace vaxelis::scene_io
