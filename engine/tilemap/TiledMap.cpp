#include "engine/tilemap/TiledMap.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "engine/core/Log.hpp"
#include "engine/renderer/SpriteRenderer.hpp"

namespace vaxelis::tiled {

namespace {
using json = nlohmann::json;

// Strips Tiled's flip-bit metadata in the high bits of each GID.
constexpr uint32_t kFlipMask = 0x1FFFFFFFu;

// Index of the tileset owning `gid` (the largest first_gid not above it), or -1
// when no tileset claims it.
int tileset_index_for_gid(const TiledMap& m, uint32_t gid) {
    int best = -1;
    for (size_t i = 0; i < m.tilesets.size(); ++i) {
        const TilesetRef& ts = m.tilesets[i];
        if (gid >= ts.first_gid &&
            (best < 0 || ts.first_gid > m.tilesets[static_cast<size_t>(best)].first_gid)) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

// A tileset can only produce UVs with a positive column count and image extent;
// anything else would divide by zero.
bool tileset_drawable(const TilesetRef& ts) {
    return ts.columns > 0 && ts.image_w > 0 && ts.image_h > 0;
}

// floor(v) as an int, saturated to [lo, hi]. Saturating also tames the inf/NaN
// that a degenerate view rect or a zero tile size would otherwise feed the cast.
int floor_clamped(float v, int lo, int hi) {
    const float f = std::floor(v);
    if (!(f > static_cast<float>(lo)))
        return lo;
    if (f >= static_cast<float>(hi))
        return hi;
    return static_cast<int>(f);
}

// Shared body of the two render() overloads; `view` is null when not culling.
void render_layers(const TiledMap& m, SpriteBatch& batch, const AABB2* view) {
    const vec2 size{static_cast<float>(m.tile_w), static_cast<float>(m.tile_h)};
    for (const TileLayer& layer : m.tile_layers) {
        if (!layer.visible)
            continue;
        const TileDrawCache& cache = layer.cache;
        if (cache.chunk_cols <= 0 || cache.chunk_rows <= 0)
            continue;

        ChunkRange window{0, 0, cache.chunk_cols - 1, cache.chunk_rows - 1};
        if (view) {
            window = visible_chunks(m, cache, *view);
            if (window.empty())
                continue;
        }

        for (const TilesetBatch& tb : cache.batches) {
            const rhi::TextureHandle tex = m.tilesets[tb.tileset].texture;
            if (!tex.valid())
                continue;
            for (const TileRun& run : tb.runs) {
                const int cx = static_cast<int>(run.chunk) % cache.chunk_cols;
                const int cy = static_cast<int>(run.chunk) / cache.chunk_cols;
                if (cx < window.x0 || cx > window.x1 || cy < window.y0 || cy > window.y1)
                    continue;
                for (uint32_t i = 0; i < run.count; ++i) {
                    const TileQuad& q = cache.quads[run.first + i];
                    batch.draw(tex, q.pos, size, q.uv, vec4{1.0f});
                }
            }
        }
    }
}

} // namespace

bool load_string(TiledMap& out, std::string_view jtext, const ImageResolver& resolver) {
    json j;
    try {
        j = json::parse(jtext);
    } catch (const std::exception& e) {
        VX_ERROR("TiledMap: JSON parse failed: {}", e.what());
        return false;
    }

    out = {};
    out.tile_w = j.value("tilewidth", 16);
    out.tile_h = j.value("tileheight", 16);
    out.width = j.value("width", 0);
    out.height = j.value("height", 0);

    for (const auto& ts : j.value("tilesets", json::array())) {
        TilesetRef t;
        t.first_gid = ts.value("firstgid", 1u);
        t.image = ts.value("image", std::string{});
        t.tile_w = ts.value("tilewidth", out.tile_w);
        t.tile_h = ts.value("tileheight", out.tile_h);
        t.columns = ts.value("columns", 1);
        t.image_w = ts.value("imagewidth", 0);
        t.image_h = ts.value("imageheight", 0);
        if (resolver && !t.image.empty())
            t.texture = resolver(t.image);
        out.tilesets.push_back(std::move(t));
    }

    for (const auto& layer : j.value("layers", json::array())) {
        const std::string type = layer.value("type", std::string{});
        if (type == "tilelayer") {
            TileLayer L;
            L.name = layer.value("name", std::string{});
            L.width = layer.value("width", 0);
            L.height = layer.value("height", 0);
            L.visible = layer.value("visible", true);
            const auto& data = layer.value("data", json::array());
            L.gids.reserve(data.size());
            for (const auto& v : data)
                L.gids.push_back(v.get<uint32_t>() & kFlipMask);
            out.tile_layers.push_back(std::move(L));
        } else if (type == "objectgroup") {
            ObjectGroup g;
            g.name = layer.value("name", std::string{});
            for (const auto& o : layer.value("objects", json::array())) {
                MapObject m;
                m.name = o.value("name", std::string{});
                m.type = o.value("type", std::string{});
                m.pos = {o.value("x", 0.0f), o.value("y", 0.0f)};
                m.size = {o.value("width", 0.0f), o.value("height", 0.0f)};
                g.objects.push_back(std::move(m));
            }
            out.object_groups.push_back(std::move(g));
        }
    }
    build_cache(out);
    return true;
}

bool load_file(TiledMap& out, std::string_view path, const ImageResolver& resolver) {
    std::ifstream f((std::string(path)));
    if (!f) {
        VX_ERROR("TiledMap: cannot open {}", path);
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return load_string(out, ss.str(), resolver);
}

void build_cache(TiledMap& m, int chunk_tiles) {
    if (chunk_tiles <= 0)
        chunk_tiles = kDefaultChunkTiles;
    const vec2 tile_size{static_cast<float>(m.tile_w), static_cast<float>(m.tile_h)};

    for (TileLayer& layer : m.tile_layers) {
        layer.cache = {};
        TileDrawCache& cache = layer.cache;
        cache.chunk_tiles = chunk_tiles;
        if (layer.width <= 0 || layer.height <= 0)
            continue;

        const size_t stride = static_cast<size_t>(layer.width);
        const size_t cells = stride * static_cast<size_t>(layer.height);
        if (layer.gids.size() < cells) {
            VX_ERROR("TiledMap: layer '{}' has {}/{} gids", layer.name, layer.gids.size(), cells);
            continue;
        }

        cache.chunk_cols = (layer.width + chunk_tiles - 1) / chunk_tiles;
        cache.chunk_rows = (layer.height + chunk_tiles - 1) / chunk_tiles;
        const size_t cols = static_cast<size_t>(cache.chunk_cols);
        const size_t chunk_count = cols * static_cast<size_t>(cache.chunk_rows);

        // Row-major index of the chunk holding tile (tx, ty).
        const auto chunk_of = [chunk_tiles, cols](int tx, int ty) {
            const size_t cx = static_cast<size_t>(tx / chunk_tiles);
            const size_t cy = static_cast<size_t>(ty / chunk_tiles);
            return cy * cols + cx;
        };

        // Pass 1: resolve each cell's tileset once and count how many tiles land
        // in every (tileset, chunk) bucket. `slot` compacts the tilesets this
        // layer actually references (usually one), so `counts` stays small.
        std::vector<int> slot_of_tileset(m.tilesets.size(), -1);
        std::vector<uint32_t> slot_tileset;
        std::vector<int> cell_slot(cells, -1); // -1 == cell draws nothing
        std::vector<uint32_t> counts;          // slot-major, chunk_count entries per slot

        for (int y = 0; y < layer.height; ++y) {
            for (int x = 0; x < layer.width; ++x) {
                const size_t cell = static_cast<size_t>(y) * stride + static_cast<size_t>(x);
                const uint32_t gid = layer.gids[cell];
                if (gid == 0)
                    continue;
                const int ti = tileset_index_for_gid(m, gid);
                if (ti < 0 || !tileset_drawable(m.tilesets[static_cast<size_t>(ti)]))
                    continue;
                int slot = slot_of_tileset[static_cast<size_t>(ti)];
                if (slot < 0) {
                    slot = static_cast<int>(slot_tileset.size());
                    slot_of_tileset[static_cast<size_t>(ti)] = slot;
                    slot_tileset.push_back(static_cast<uint32_t>(ti));
                    // Slot-major layout, so a new slot only appends.
                    counts.resize((static_cast<size_t>(slot) + 1) * chunk_count, 0);
                }
                cell_slot[cell] = slot;
                ++counts[static_cast<size_t>(slot) * chunk_count + chunk_of(x, y)];
            }
        }

        // Quads are laid out slot-major then chunk-major, which makes every
        // (slot, chunk) bucket one contiguous run.
        std::vector<uint32_t> cursor(counts.size(), 0);
        uint32_t offset = 0;
        cache.batches.reserve(slot_tileset.size());
        for (size_t slot = 0; slot < slot_tileset.size(); ++slot) {
            TilesetBatch tb;
            tb.tileset = slot_tileset[slot];
            for (size_t c = 0; c < chunk_count; ++c) {
                const uint32_t n = counts[slot * chunk_count + c];
                if (n == 0)
                    continue;
                tb.runs.push_back({static_cast<uint32_t>(c), offset, n});
                cursor[slot * chunk_count + c] = offset;
                offset += n;
            }
            cache.batches.push_back(std::move(tb));
        }
        cache.quads.resize(offset);

        // Pass 2: write each tile into the slice reserved for its bucket.
        for (int y = 0; y < layer.height; ++y) {
            for (int x = 0; x < layer.width; ++x) {
                const size_t cell = static_cast<size_t>(y) * stride + static_cast<size_t>(x);
                const int slot = cell_slot[cell];
                if (slot < 0)
                    continue;
                const TilesetRef& ts = m.tilesets[slot_tileset[static_cast<size_t>(slot)]];
                const uint32_t local = layer.gids[cell] - ts.first_gid;
                const int col = static_cast<int>(local % ts.columns);
                const int row = static_cast<int>(local / ts.columns);
                const float iw = static_cast<float>(ts.image_w);
                const float ih = static_cast<float>(ts.image_h);
                const float u0 = static_cast<float>(col * ts.tile_w) / iw;
                const float v0 = static_cast<float>(row * ts.tile_h) / ih;
                const float u1 = static_cast<float>((col + 1) * ts.tile_w) / iw;
                const float v1 = static_cast<float>((row + 1) * ts.tile_h) / ih;
                const vec2 pos{(static_cast<float>(x) + 0.5f) * tile_size.x,
                               (static_cast<float>(y) + 0.5f) * tile_size.y};
                const size_t bucket = static_cast<size_t>(slot) * chunk_count + chunk_of(x, y);
                cache.quads[cursor[bucket]++] = TileQuad{pos, vec4{u0, v0, u1, v1}};
            }
        }
    }
}

ChunkRange visible_chunks(const TiledMap& m, const TileDrawCache& cache, const AABB2& view) {
    ChunkRange r;
    if (cache.chunk_cols <= 0 || cache.chunk_rows <= 0)
        return r; // default-constructed range is empty
    const float cw = static_cast<float>(cache.chunk_tiles) * static_cast<float>(m.tile_w);
    const float chh = static_cast<float>(cache.chunk_tiles) * static_cast<float>(m.tile_h);
    // A view past the last chunk clamps to one-past-the-end, and one entirely
    // before the layer clamps to -1; either way the range comes out empty.
    r.x0 = floor_clamped(view.min.x / cw, 0, cache.chunk_cols);
    r.x1 = floor_clamped(view.max.x / cw, -1, cache.chunk_cols - 1);
    r.y0 = floor_clamped(view.min.y / chh, 0, cache.chunk_rows);
    r.y1 = floor_clamped(view.max.y / chh, -1, cache.chunk_rows - 1);
    return r;
}

void render(const TiledMap& m, SpriteBatch& batch) {
    render_layers(m, batch, nullptr);
}

void render(const TiledMap& m, SpriteBatch& batch, const AABB2& view) {
    render_layers(m, batch, &view);
}

} // namespace vaxelis::tiled
