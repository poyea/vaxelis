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

/// One tile ready for submission: the world-space center of the quad plus its
/// uv rect in SpriteBatch's convention (v_bottom in .y, v_top in .w). Every tile
/// is drawn at the map's tile size, so no per-quad extent is stored.
struct TileQuad {
    vec2 pos{0.0f, 0.0f};
    vec4 uv{0.0f, 0.0f, 1.0f, 1.0f};
};

/// The tiles of one chunk that come from one tileset: a contiguous
/// [first, first + count) slice of TileDrawCache::quads.
struct TileRun {
    uint32_t chunk{0}; ///< row-major index into the layer's chunk grid
    uint32_t first{0};
    uint32_t count{0};
};

/// Every run in a layer that samples one tileset, ascending by chunk. Grouping
/// this way costs one SpriteBatch flush per tileset per layer rather than one
/// per chunk.
struct TilesetBatch {
    uint32_t tileset{0}; ///< index into TiledMap::tilesets
    std::vector<TileRun> runs;
};

/// Pre-resolved draw data for one tile layer: tile positions and UVs are
/// computed once at load instead of once per frame, and the tiles are bucketed
/// into a `chunk_tiles` square grid so off-screen chunks can be skipped whole.
///
/// Chunks are laid out row-major, so a chunk's world AABB follows from its index
/// and needs no storage. Texture handles are deliberately *not* baked in:
/// TilesetBatch::tileset indexes TiledMap::tilesets, so a hot-reloaded texture is
/// picked up without a rebuild.
struct TileDrawCache {
    int chunk_tiles{0};
    int chunk_cols{0};
    int chunk_rows{0};
    std::vector<TileQuad> quads; ///< tileset-major, then chunk-major
    std::vector<TilesetBatch> batches;
};

/// One orthogonal tile layer.
struct TileLayer {
    std::string name;
    int width{0};
    int height{0};
    std::vector<uint32_t> gids; ///< row-major, 0 == empty
    bool visible{true};
    TileDrawCache cache; ///< derived from `gids`; see tiled::build_cache()
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

/// Chunk edge length in tiles, used when none is given.
inline constexpr int kDefaultChunkTiles = 16;

/// Inclusive window of chunk indices. Empty when the view misses the layer.
struct ChunkRange {
    int x0{0};
    int y0{0};
    int x1{-1};
    int y1{-1};

    constexpr bool empty() const { return x0 > x1 || y0 > y1; }
};

/// Parses .tmj JSON text into `out`. Returns false on malformed input.
bool load_string(TiledMap& out, std::string_view json, const ImageResolver&);
/// Reads `path` and feeds it through load_string().
bool load_file(TiledMap& out, std::string_view path, const ImageResolver&);

/// (Re)builds TileLayer::cache for every layer. The load_* functions call this,
/// so it is only needed after editing `gids` by hand or changing a tileset's
/// grid metrics. Swapping a tileset's texture handle does *not* need a rebuild.
/// Layers whose cache is empty draw nothing.
void build_cache(TiledMap&, int chunk_tiles = kDefaultChunkTiles);

/// Chunks of `cache` overlapping the world-space rect `view`.
ChunkRange visible_chunks(const TiledMap&, const TileDrawCache& cache, const AABB2& view);

/// Renders every visible tile layer into `batch`. Caller controls batch begin/end.
void render(const TiledMap&, SpriteBatch& batch);

/// As render(), but skips chunks that fall outside `view` (world-space pixels).
void render(const TiledMap&, SpriteBatch& batch, const AABB2& view);

} // namespace tiled

} // namespace vaxelis
