// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

#include <string>
#include <string_view>

namespace vaxelis {

class Scene;

/// JSON serialization for Scene. Sprite textures are stored as `texture_key`
/// strings; the runtime is responsible for re-resolving them after load via
/// `host_resolve` (e.g. an asset cache lookup).
///
/// File format (top-level):
/// @code
/// { "nodes": [ { "id": "<uuid>", "name": "...", "parent": "<uuid>"|null,
///                "transform": { "pos": [x,y], "rot": r, "scale": [x,y] },
///                "sprite": { "texture_key": "...", "size": [w,h], ... } }, ... ] }
/// @endcode
/// `id`/`parent` are stable Uuids (see Components::Id); `parent == null` means
/// the implicit root. Legacy files using integer ids (parent 0 == root) still
/// load, but their nodes are assigned fresh uuids on import.
namespace scene_io {

/// Serializes the whole scene to a JSON string.
std::string to_json(const Scene&, int indent = 2);
/// Parses `json` and appends the nodes under the scene's root.
/// @return false on malformed input.
bool from_json(Scene&, std::string_view json);

/// Writes to_json() output to `path`. Returns false on I/O failure.
bool save_file(const Scene&, std::string_view path);
/// Reads `path` and feeds it through from_json().
bool load_file(Scene&, std::string_view path);

} // namespace scene_io

} // namespace vaxelis
