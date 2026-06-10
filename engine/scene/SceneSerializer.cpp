#include "engine/scene/SceneSerializer.hpp"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "engine/core/Log.hpp"
#include "engine/scene/Scene.hpp"

namespace vaxelis::scene_io {

namespace {

using json = nlohmann::json;

json save_vec2(const vec2& v) {
    return json::array({v.x, v.y});
}
json save_vec4(const vec4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}
vec2 load_vec2(const json& j) {
    return vec2{j.at(0).get<float>(), j.at(1).get<float>()};
}
vec4 load_vec4(const json& j) {
    return vec4{j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(),
                j.at(3).get<float>()};
}

} // namespace

std::string to_json(const Scene& s, int indent) {
    json out;
    out["nodes"] = json::array();
    s.for_each([&](entt::entity e) {
        if (e == s.root())
            return;
        const auto& reg = s.registry();
        json node;
        // Stable per-node identity; survives reorder/merge (see Components::Id).
        node["id"] = to_string(reg.get<Id>(e).uuid);
        node["name"] = reg.get<Name>(e).value;
        const auto parent = reg.get<Hierarchy>(e).parent;
        node["parent"] = (parent == s.root() || parent == entt::null)
                             ? json(nullptr)
                             : json(to_string(reg.get<Id>(parent).uuid));

        const auto& t = reg.get<Transform2D>(e);
        node["transform"] = {
            {"pos", save_vec2(t.position)},
            {"rot", t.rotation},
            {"scale", save_vec2(t.scale)},
        };

        if (const auto* sp = reg.try_get<SpriteComponent>(e)) {
            node["sprite"] = {
                {"texture_key", sp->texture_key},
                {"size", save_vec2(sp->size)},
                {"uv_rect", save_vec4(sp->uv_rect)},
                {"color", save_vec4(sp->color)},
                {"z_order", sp->z_order},
                {"visible", sp->visible},
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

    // Maps a node's "id"/"parent" JSON value to a stable lookup token. New
    // files carry a uuid string; legacy files carried an integer (rendered
    // "#<n>"). Returns empty for anything else (e.g. a null/absent parent).
    auto to_token = [](const json& v) -> std::string {
        if (v.is_string())
            return v.get<std::string>();
        if (v.is_number_integer())
            return "#" + std::to_string(v.get<long long>());
        return {};
    };

    // Existing uuids (the scene may already hold nodes; from_json appends).
    // Incoming ids that collide are reminted so merges/instancing stay unique;
    // a load into a fresh scene preserves every stored uuid.
    std::unordered_set<Uuid> used;
    for (auto e : s.registry().view<const Id>())
        used.insert(s.registry().get<const Id>(e).uuid);

    // Two-pass: create all entities (so parent refs resolve), then wire up
    // parents and populate components.
    std::unordered_map<std::string, entt::entity> by_token;
    by_token["#0"] = s.root(); // legacy root reference

    for (const auto& node : j["nodes"]) {
        if (!node.contains("id"))
            continue;
        const std::string token = to_token(node["id"]);
        if (token.empty() || token == "#0")
            continue;
        // Adopt the stored uuid unless it is malformed (legacy integer ids),
        // null, or already taken; create_node mints a fresh one for {}.
        Uuid uuid = node["id"].is_string() ? uuid_from_string(token) : Uuid{};
        if (uuid.valid() && !used.insert(uuid).second)
            uuid = {}; // collision: let create_node remint
        auto e = s.create_node(node.value("name", std::string{"Node"}), entt::null, uuid);
        used.insert(s.registry().get<Id>(e).uuid); // covers the reminted case
        by_token[token] = e;
    }

    for (const auto& node : j["nodes"]) {
        if (!node.contains("id"))
            continue;
        const std::string token = to_token(node["id"]);
        if (token.empty() || token == "#0")
            continue;
        auto it = by_token.find(token);
        if (it == by_token.end())
            continue;
        auto e = it->second;

        if (node.contains("parent")) {
            auto pit = by_token.find(to_token(node["parent"]));
            if (pit != by_token.end())
                s.set_parent(e, pit->second);
        }

        if (node.contains("transform")) {
            const auto& tj = node["transform"];
            auto& t = s.registry().get<Transform2D>(e);
            if (tj.contains("pos"))
                t.position = load_vec2(tj["pos"]);
            if (tj.contains("rot"))
                t.rotation = tj["rot"].get<float>();
            if (tj.contains("scale"))
                t.scale = load_vec2(tj["scale"]);
        }
        if (node.contains("sprite")) {
            const auto& sj = node["sprite"];
            SpriteComponent sp;
            sp.texture_key = sj.value("texture_key", std::string{});
            if (sj.contains("size"))
                sp.size = load_vec2(sj["size"]);
            if (sj.contains("uv_rect"))
                sp.uv_rect = load_vec4(sj["uv_rect"]);
            if (sj.contains("color"))
                sp.color = load_vec4(sj["color"]);
            sp.z_order = sj.value("z_order", 0);
            sp.visible = sj.value("visible", true);
            s.registry().emplace<SpriteComponent>(e, std::move(sp));
        }
    }
    return true;
}

bool save_file(const Scene& s, std::string_view path) {
    std::ofstream f((std::string(path)));
    if (!f) {
        VX_ERROR("Scene save: cannot open {}", path);
        return false;
    }
    f << to_json(s);
    return f.good();
}

bool load_file(Scene& s, std::string_view path) {
    std::ifstream f((std::string(path)));
    if (!f) {
        VX_ERROR("Scene load: cannot open {}", path);
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return from_json(s, ss.str());
}

} // namespace vaxelis::scene_io
