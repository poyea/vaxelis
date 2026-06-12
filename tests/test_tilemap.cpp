#include <gtest/gtest.h>

#include "engine/tilemap/TiledMap.hpp"

using namespace vaxelis;

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

TEST(TiledMap, StripsTiledFlipFlagsFromGids) {
    // GID with horizontal-flip bit (0x80000000) set should reduce to 3.
    constexpr const char* kJson = R"({
        "width": 1, "height": 1, "tilewidth": 16, "tileheight": 16,
        "tilesets": [{"firstgid":1,"image":"a","imagewidth":16,"imageheight":16,"tilewidth":16,"tileheight":16,"columns":1}],
        "layers": [{"type":"tilelayer","name":"l","width":1,"height":1,"data":[2147483651]}]
    })";
    TiledMap m;
    ASSERT_TRUE(tiled::load_string(m, kJson, [](const std::string&) { return rhi::TextureHandle{}; }));
    ASSERT_EQ(m.tile_layers.size(), 1u);
    EXPECT_EQ(m.tile_layers[0].gids[0], 3u);
}
