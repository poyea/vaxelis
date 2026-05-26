#pragma once

#include <string>
#include <string_view>

namespace vaxelis {

class Scene;

// JSON serialization for Scene. Sprite textures are stored as `texture_key`
// strings; the runtime is responsible for re-resolving them after load via
// `host_resolve` (e.g. an asset cache lookup).
//
// File format (top-level):
// { "nodes": [ { "id": 1, "name": "...", "parent": 0,
//                "transform": { "pos": [x,y], "rot": r, "scale": [x,y] },
//                "sprite": { "texture_key": "...", "size": [w,h], ... } }, ... ] }
// `id` is per-file (not entt id), `parent == 0` means root.
namespace scene_io {

std::string to_json(const Scene&, int indent = 2);
bool        from_json(Scene&, std::string_view json);  // appends under root_

bool        save_file(const Scene&, std::string_view path);
bool        load_file(Scene&, std::string_view path);

}  // namespace scene_io

}  // namespace vaxelis
