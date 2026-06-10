#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/math/Math.hpp"
#include "engine/rhi/Rhi.hpp"

namespace vaxelis {

class SpriteBatch;

/// Tileset reference: the source `image` string plus the runtime-resolved
/// texture and the tile-grid geometry needed to compute UVs.
struct TilesetRef {
    std::string image;
    rhi::TextureHandle texture{};
    uint32_t first_gid{1};
    int tile_w{16};
    int tile_h{16};
    int columns{1};
    int image_w{0};
    int image_h{0};
};

/// One orthogonal tile layer.
struct TileLayer {
    std::string name;
    int width{0};
    int height{0};
    std::vector<uint32_t> gids; ///< row-major, 0 == empty
    bool visible{true};
};

/// Placed object from an object group (position/size in pixels).
struct MapObject {
    std::string name;
    std::string type;
    vec2 pos{0.0f, 0.0f};
    vec2 size{0.0f, 0.0f};
};

/// Named collection of map objects.
struct ObjectGroup {
    std::string name;
    std::vector<MapObject> objects;
};

/// In-memory Tiled map: tile grid metrics plus parsed tilesets, layers, and
/// object groups.
struct TiledMap {
    int tile_w{16};
    int tile_h{16};
    int width{0};
    int height{0};
    std::vector<TilesetRef> tilesets;
    std::vector<TileLayer> tile_layers;
    std::vector<ObjectGroup> object_groups;

    /// Map extent in pixels.
    vec2 world_size() const {
        return {static_cast<float>(width * tile_w), static_cast<float>(height * tile_h)};
    }
};

/// Minimal Tiled .tmj loader. Supports orthogonal CSV-style tile layers and
/// object groups. Tilesets are referenced by an `image` string; the host
/// resolves that string to a texture handle via the `image_resolver` callback
/// passed to load_*.
///
/// Flip flags and tile rotation are stripped (we only honor the GID payload).
namespace tiled {

/// Maps a tileset `image` string to a texture handle (host-provided).
using ImageResolver = std::function<rhi::TextureHandle(const std::string& image)>;

/// Parses .tmj JSON text into `out`. Returns false on malformed input.
bool load_string(TiledMap& out, std::string_view json, const ImageResolver&);
/// Reads `path` and feeds it through load_string().
bool load_file(TiledMap& out, std::string_view path, const ImageResolver&);

/// Renders every visible tile layer into `batch`. Caller controls batch begin/end.
void render(const TiledMap&, SpriteBatch& batch);

} // namespace tiled

} // namespace vaxelis
