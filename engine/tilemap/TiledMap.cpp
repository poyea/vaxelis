#include "engine/tilemap/TiledMap.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "engine/core/Log.hpp"
#include "engine/renderer/SpriteRenderer.hpp"

namespace vaxelis::tiled {

namespace {
using json = nlohmann::json;

// Strips Tiled's flip-bit metadata in the high bits of each GID.
constexpr uint32_t kFlipMask = 0x1FFFFFFFu;

const TilesetRef* find_tileset_for_gid(const TiledMap& m, uint32_t gid) {
    const TilesetRef* best = nullptr;
    for (const auto& ts : m.tilesets) {
        if (gid >= ts.first_gid && (!best || ts.first_gid > best->first_gid)) {
            best = &ts;
        }
    }
    return best;
}

}  // namespace

bool load_string(TiledMap& out, std::string_view jtext, const ImageResolver& resolver) {
    json j;
    try { j = json::parse(jtext); }
    catch (const std::exception& e) {
        VX_ERROR("TiledMap: JSON parse failed: {}", e.what());
        return false;
    }

    out = {};
    out.tile_w = j.value("tilewidth",  16);
    out.tile_h = j.value("tileheight", 16);
    out.width  = j.value("width",  0);
    out.height = j.value("height", 0);

    for (const auto& ts : j.value("tilesets", json::array())) {
        TilesetRef t;
        t.first_gid = ts.value("firstgid", 1u);
        t.image     = ts.value("image", std::string{});
        t.tile_w    = ts.value("tilewidth",  out.tile_w);
        t.tile_h    = ts.value("tileheight", out.tile_h);
        t.columns   = ts.value("columns", 1);
        t.image_w   = ts.value("imagewidth",  0);
        t.image_h   = ts.value("imageheight", 0);
        if (resolver && !t.image.empty()) t.texture = resolver(t.image);
        out.tilesets.push_back(std::move(t));
    }

    for (const auto& layer : j.value("layers", json::array())) {
        const std::string type = layer.value("type", std::string{});
        if (type == "tilelayer") {
            TileLayer L;
            L.name    = layer.value("name", std::string{});
            L.width   = layer.value("width",  0);
            L.height  = layer.value("height", 0);
            L.visible = layer.value("visible", true);
            const auto& data = layer.value("data", json::array());
            L.gids.reserve(data.size());
            for (const auto& v : data) L.gids.push_back(v.get<uint32_t>() & kFlipMask);
            out.tile_layers.push_back(std::move(L));
        } else if (type == "objectgroup") {
            ObjectGroup g;
            g.name = layer.value("name", std::string{});
            for (const auto& o : layer.value("objects", json::array())) {
                MapObject m;
                m.name = o.value("name", std::string{});
                m.type = o.value("type", std::string{});
                m.pos  = { o.value("x", 0.0f), o.value("y", 0.0f) };
                m.size = { o.value("width", 0.0f), o.value("height", 0.0f) };
                g.objects.push_back(std::move(m));
            }
            out.object_groups.push_back(std::move(g));
        }
    }
    return true;
}

bool load_file(TiledMap& out, std::string_view path, const ImageResolver& resolver) {
    std::ifstream f((std::string(path)));
    if (!f) { VX_ERROR("TiledMap: cannot open {}", path); return false; }
    std::stringstream ss; ss << f.rdbuf();
    return load_string(out, ss.str(), resolver);
}

void render(const TiledMap& m, SpriteBatch& batch) {
    for (const auto& layer : m.tile_layers) {
        if (!layer.visible) continue;
        for (int y = 0; y < layer.height; ++y) {
            for (int x = 0; x < layer.width; ++x) {
                const uint32_t gid = layer.gids[static_cast<size_t>(y) * layer.width + x];
                if (gid == 0) continue;
                const auto* ts = find_tileset_for_gid(m, gid);
                if (!ts || !ts->texture.valid() || ts->columns <= 0) continue;
                const uint32_t local = gid - ts->first_gid;
                const int col = static_cast<int>(local % ts->columns);
                const int row = static_cast<int>(local / ts->columns);
                const float u0 = (col * ts->tile_w) / static_cast<float>(ts->image_w);
                const float v0 = (row * ts->tile_h) / static_cast<float>(ts->image_h);
                const float u1 = ((col + 1) * ts->tile_w) / static_cast<float>(ts->image_w);
                const float v1 = ((row + 1) * ts->tile_h) / static_cast<float>(ts->image_h);
                const vec2 pos{
                    (x + 0.5f) * m.tile_w,
                    (y + 0.5f) * m.tile_h,
                };
                const vec2 size{ static_cast<float>(m.tile_w), static_cast<float>(m.tile_h) };
                // SpriteBatch writes uv_rect.w to the top-screen vertex, so
                // pass v_bottom in .y and v_top in .w.
                batch.draw(ts->texture, pos, size,
                           vec4{u0, v1, u1, v0}, vec4{1.0f});
            }
        }
    }
}

}  // namespace vaxelis::tiled
