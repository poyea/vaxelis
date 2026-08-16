// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <gtest/gtest.h>

#include "engine/tilemap/TiledMap.hpp"

using namespace vaxelis;

namespace {

// 4x4 tiles of 16px = a 64x64 world, every cell filled from a single 1x1
// tileset. Rebuilt with 2x2-tile chunks, so four 32px-square chunks.
TiledMap make_full_4x4() {
    constexpr const char* kJson = R"({
        "width": 4, "height": 4, "tilewidth": 16, "tileheight": 16,
        "tilesets": [{"firstgid":1,"image":"a","imagewidth":16,"imageheight":16,"tilewidth":16,"tileheight":16,"columns":1}],
        "layers": [{"type":"tilelayer","name":"l","width":4,"height":4,
                    "data":[1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1]}]
    })";
    TiledMap m;
    tiled::load_string(m, kJson, [](const std::string&) { return rhi::TextureHandle{7}; });
    tiled::build_cache(m, 2);
    return m;
}

} // namespace

TEST(TiledMap, ParsesInlineJsonWithOneTileLayerPlusOneObject) {
    constexpr const char* kJson = R"({
        "width": 3, "height": 2, "tilewidth": 16, "tileheight": 16,
        "tilesets": [
            { "firstgid": 1, "image": "atlas", "imagewidth": 32, "imageheight": 32,
              "tilewidth": 16, "tileheight": 16, "columns": 2 }
        ],
        "layers": [
            { "type": "tilelayer", "name": "world", "width": 3, "height": 2,
              "data": [0,1,2, 3,4,0] },
            { "type": "objectgroup", "name": "spawns", "objects": [
                { "name": "player", "type": "spawn", "x": 8, "y": 16, "width": 0, "height": 0 }
            ]}
        ]
    })";

    TiledMap m;
    ASSERT_TRUE(
        tiled::load_string(m, kJson, [](const std::string&) { return rhi::TextureHandle{42}; }));

    EXPECT_EQ(m.width, 3);
    EXPECT_EQ(m.height, 2);
    EXPECT_EQ(m.tile_w, 16);
    ASSERT_EQ(m.tilesets.size(), 1u);
    EXPECT_EQ(m.tilesets[0].image, "atlas");
    EXPECT_EQ(m.tilesets[0].texture.id, 42u);

    ASSERT_EQ(m.tile_layers.size(), 1u);
    ASSERT_EQ(m.tile_layers[0].gids.size(), 6u);
    EXPECT_EQ(m.tile_layers[0].gids[1], 1u);

    ASSERT_EQ(m.object_groups.size(), 1u);
    ASSERT_EQ(m.object_groups[0].objects.size(), 1u);
    EXPECT_EQ(m.object_groups[0].objects[0].name, "player");
    EXPECT_FLOAT_EQ(m.object_groups[0].objects[0].pos.x, 8.0f);
}

TEST(TiledMap, LoadBuildsAChunkedDrawCache) {
    constexpr const char* kJson = R"({
        "width": 3, "height": 2, "tilewidth": 16, "tileheight": 16,
        "tilesets": [
            { "firstgid": 1, "image": "atlas", "imagewidth": 32, "imageheight": 32,
              "tilewidth": 16, "tileheight": 16, "columns": 2 }
        ],
        "layers": [
            { "type": "tilelayer", "name": "world", "width": 3, "height": 2,
              "data": [0,1,2, 3,4,0] }
        ]
    })";

    TiledMap m;
    ASSERT_TRUE(
        tiled::load_string(m, kJson, [](const std::string&) { return rhi::TextureHandle{42}; }));
    ASSERT_EQ(m.tile_layers.size(), 1u);

    const auto& cache = m.tile_layers[0].cache;
    // 6 cells, two of them empty; the map fits inside a single 16x16 chunk.
    EXPECT_EQ(cache.quads.size(), 4u);
    EXPECT_EQ(cache.chunk_cols, 1);
    EXPECT_EQ(cache.chunk_rows, 1);
    ASSERT_EQ(cache.batches.size(), 1u);
    EXPECT_EQ(cache.batches[0].tileset, 0u);
    ASSERT_EQ(cache.batches[0].runs.size(), 1u);
    EXPECT_EQ(cache.batches[0].runs[0].chunk, 0u);
    EXPECT_EQ(cache.batches[0].runs[0].first, 0u);
    EXPECT_EQ(cache.batches[0].runs[0].count, 4u);

    // First quad is gid 1 at cell (1,0): centered in its cell, sampling the
    // top-left tile of a 2x2 atlas as (min_u, min_v, max_u, max_v). The rect is
    // inset by half a texel of the 32x32 atlas so it stops short of the edge it
    // shares with the neighbouring tile: 0.5/32 in, 15.5/32 out.
    const auto& q = cache.quads[0];
    EXPECT_FLOAT_EQ(q.pos.x, 24.0f);
    EXPECT_FLOAT_EQ(q.pos.y, 8.0f);
    EXPECT_FLOAT_EQ(q.uv.x, 0.015625f);
    EXPECT_FLOAT_EQ(q.uv.y, 0.015625f);
    EXPECT_FLOAT_EQ(q.uv.z, 0.484375f);
    EXPECT_FLOAT_EQ(q.uv.w, 0.484375f);
}

TEST(TiledMap, TileUvsStopShortOfTheEdgeSharedWithTheNextTile) {
    // Two 16px tiles side by side in a 32x16 atlas. Tile 0's max_u and tile 1's
    // min_u would both be exactly 0.5 without the inset, so a sample on that
    // boundary could land in either tile.
    constexpr const char* kJson = R"({
        "width": 2, "height": 1, "tilewidth": 16, "tileheight": 16,
        "tilesets": [{"firstgid":1,"image":"a","imagewidth":32,"imageheight":16,"tilewidth":16,"tileheight":16,"columns":2}],
        "layers": [{"type":"tilelayer","name":"l","width":2,"height":1,"data":[1,2]}]
    })";
    TiledMap m;
    ASSERT_TRUE(
        tiled::load_string(m, kJson, [](const std::string&) { return rhi::TextureHandle{1}; }));
    ASSERT_EQ(m.tile_layers.size(), 1u);
    const auto& quads = m.tile_layers[0].cache.quads;
    ASSERT_EQ(quads.size(), 2u);

    EXPECT_LT(quads[0].uv.z, 0.5f); // left tile stops before the seam
    EXPECT_GT(quads[1].uv.x, 0.5f); // right tile starts after it
    EXPECT_FLOAT_EQ(quads[0].uv.z, 15.5f / 32.0f);
    EXPECT_FLOAT_EQ(quads[1].uv.x, 16.5f / 32.0f);
    // The outer edges pull in as well; the atlas has no neighbour there, but a
    // uniform inset keeps every tile the same sampled size.
    EXPECT_FLOAT_EQ(quads[0].uv.x, 0.5f / 32.0f);
    EXPECT_FLOAT_EQ(quads[1].uv.z, 31.5f / 32.0f);
}

TEST(TiledMap, ATilesetWithNoUsableGeometryProducesNoQuads) {
    // tilewidth 0 makes every derived uv meaningless, and would invert the
    // half-texel inset into a rect whose min exceeds its max.
    constexpr const char* kJson = R"({
        "width": 1, "height": 1, "tilewidth": 16, "tileheight": 16,
        "tilesets": [{"firstgid":1,"image":"a","imagewidth":16,"imageheight":16,"tilewidth":0,"tileheight":0,"columns":1}],
        "layers": [{"type":"tilelayer","name":"l","width":1,"height":1,"data":[1]}]
    })";
    TiledMap m;
    ASSERT_TRUE(
        tiled::load_string(m, kJson, [](const std::string&) { return rhi::TextureHandle{1}; }));
    ASSERT_EQ(m.tile_layers.size(), 1u);
    EXPECT_TRUE(m.tile_layers[0].cache.quads.empty());
}

TEST(TiledMap, TilesAreBucketedPerChunk) {
    const TiledMap m = make_full_4x4();
    ASSERT_EQ(m.tile_layers.size(), 1u);

    const auto& cache = m.tile_layers[0].cache;
    EXPECT_EQ(cache.chunk_cols, 2);
    EXPECT_EQ(cache.chunk_rows, 2);
    EXPECT_EQ(cache.quads.size(), 16u);
    ASSERT_EQ(cache.batches.size(), 1u);

    // One run per chunk, four tiles each, laid out contiguously in chunk order.
    const auto& runs = cache.batches[0].runs;
    ASSERT_EQ(runs.size(), 4u);
    for (uint32_t i = 0; i < runs.size(); ++i) {
        EXPECT_EQ(runs[i].chunk, i);
        EXPECT_EQ(runs[i].first, i * 4);
        EXPECT_EQ(runs[i].count, 4u);
    }
}

TEST(TiledMap, VisibleChunksWindowsTheViewRect) {
    const TiledMap m = make_full_4x4();
    ASSERT_EQ(m.tile_layers.size(), 1u);
    const auto& cache = m.tile_layers[0].cache;

    // A view inside the first chunk selects only that chunk.
    auto r = tiled::visible_chunks(m, cache, AABB2{{0.0f, 0.0f}, {31.0f, 31.0f}});
    EXPECT_FALSE(r.empty());
    EXPECT_EQ(r.x0, 0);
    EXPECT_EQ(r.x1, 0);
    EXPECT_EQ(r.y0, 0);
    EXPECT_EQ(r.y1, 0);

    // A view spanning the map selects every chunk.
    r = tiled::visible_chunks(m, cache, AABB2{{0.0f, 0.0f}, {64.0f, 64.0f}});
    EXPECT_EQ(r.x0, 0);
    EXPECT_EQ(r.x1, 1);
    EXPECT_EQ(r.y0, 0);
    EXPECT_EQ(r.y1, 1);

    // Views off either end of the map select nothing.
    r = tiled::visible_chunks(m, cache, AABB2{{200.0f, 200.0f}, {300.0f, 300.0f}});
    EXPECT_TRUE(r.empty());
    r = tiled::visible_chunks(m, cache, AABB2{{-300.0f, -300.0f}, {-1.0f, -1.0f}});
    EXPECT_TRUE(r.empty());
}

TEST(TiledMap, CacheSkipsGidsNoTilesetClaims) {
    // firstgid 10, so gid 1 belongs to nothing and must not produce a quad.
    constexpr const char* kJson = R"({
        "width": 2, "height": 1, "tilewidth": 16, "tileheight": 16,
        "tilesets": [{"firstgid":10,"image":"a","imagewidth":16,"imageheight":16,"tilewidth":16,"tileheight":16,"columns":1}],
        "layers": [{"type":"tilelayer","name":"l","width":2,"height":1,"data":[1,10]}]
    })";

    TiledMap m;
    ASSERT_TRUE(
        tiled::load_string(m, kJson, [](const std::string&) { return rhi::TextureHandle{1}; }));
    ASSERT_EQ(m.tile_layers.size(), 1u);
    ASSERT_EQ(m.tile_layers[0].cache.quads.size(), 1u);
    EXPECT_FLOAT_EQ(m.tile_layers[0].cache.quads[0].pos.x, 24.0f);
}

TEST(TiledMap, StripsTiledFlipFlagsFromGids) {
    // GID with horizontal-flip bit (0x80000000) set should reduce to 3.
    constexpr const char* kJson = R"({
        "width": 1, "height": 1, "tilewidth": 16, "tileheight": 16,
        "tilesets": [{"firstgid":1,"image":"a","imagewidth":16,"imageheight":16,"tilewidth":16,"tileheight":16,"columns":1}],
        "layers": [{"type":"tilelayer","name":"l","width":1,"height":1,"data":[2147483651]}]
    })";
    TiledMap m;
    ASSERT_TRUE(
        tiled::load_string(m, kJson, [](const std::string&) { return rhi::TextureHandle{}; }));
    ASSERT_EQ(m.tile_layers.size(), 1u);
    EXPECT_EQ(m.tile_layers[0].gids[0], 3u);
}
