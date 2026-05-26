#include <catch2/catch_test_macros.hpp>

#include "engine/tilemap/TiledMap.hpp"

using namespace vaxelis;

TEST_CASE("TiledMap: parses inline JSON with one tile layer + one object") {
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
    REQUIRE(
        tiled::load_string(m, kJson, [](const std::string&) { return rhi::TextureHandle{42}; }));

    REQUIRE(m.width == 3);
    REQUIRE(m.height == 2);
    REQUIRE(m.tile_w == 16);
    REQUIRE(m.tilesets.size() == 1);
    REQUIRE(m.tilesets[0].image == "atlas");
    REQUIRE(m.tilesets[0].texture.id == 42);

    REQUIRE(m.tile_layers.size() == 1);
    REQUIRE(m.tile_layers[0].gids.size() == 6);
    REQUIRE(m.tile_layers[0].gids[1] == 1);

    REQUIRE(m.object_groups.size() == 1);
    REQUIRE(m.object_groups[0].objects.size() == 1);
    REQUIRE(m.object_groups[0].objects[0].name == "player");
    REQUIRE(m.object_groups[0].objects[0].pos.x == 8.0f);
}

TEST_CASE("TiledMap: strips Tiled flip flags from GIDs") {
    // GID with horizontal-flip bit (0x80000000) set should reduce to 3.
    constexpr const char* kJson = R"({
        "width": 1, "height": 1, "tilewidth": 16, "tileheight": 16,
        "tilesets": [{"firstgid":1,"image":"a","imagewidth":16,"imageheight":16,"tilewidth":16,"tileheight":16,"columns":1}],
        "layers": [{"type":"tilelayer","name":"l","width":1,"height":1,"data":[2147483651]}]
    })";
    TiledMap m;
    REQUIRE(tiled::load_string(m, kJson, [](const std::string&) { return rhi::TextureHandle{}; }));
    REQUIRE(m.tile_layers[0].gids[0] == 3u);
}
