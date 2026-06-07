#include <catch2/catch_test_macros.hpp>

#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneSerializer.hpp"

using namespace vaxelis;

TEST_CASE("Scene: create/destroy nodes and hierarchy") {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    auto c = s.create_node("C", b);

    REQUIRE(s.registry().get<Hierarchy>(a).parent == s.root());
    REQUIRE(s.registry().get<Hierarchy>(b).parent == a);
    REQUIRE(s.registry().get<Hierarchy>(c).parent == b);
    REQUIRE(s.registry().get<Hierarchy>(a).children.size() == 1);

    // Cascades to b and c.
    s.destroy_node(a);
    REQUIRE_FALSE(s.registry().valid(a));
    REQUIRE_FALSE(s.registry().valid(b));
    REQUIRE_FALSE(s.registry().valid(c));
    REQUIRE(s.registry().get<Hierarchy>(s.root()).children.empty());
}

TEST_CASE("Scene: set_parent rejects cycles") {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    // Reparenting a under b would form a cycle — must be rejected.
    s.set_parent(a, b);
    REQUIRE(s.registry().get<Hierarchy>(a).parent == s.root());
}

TEST_CASE("Scene: world_matrix composes parent transforms") {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    s.registry().get<Transform2D>(a).position = {10.0f, 0.0f};
    s.registry().get<Transform2D>(b).position = {5.0f, 0.0f};
    auto w = s.world_matrix(b);
    REQUIRE(w[3].x == 15.0f);
}

TEST_CASE("Scene: JSON round-trip preserves hierarchy and components") {
    Scene s;
    auto a = s.create_node("Parent");
    auto b = s.create_node("Child", a);
    auto& tb = s.registry().get<Transform2D>(b);
    tb.position = {42.0f, -7.0f};
    tb.rotation = 1.5f;
    auto& sp = s.registry().emplace<SpriteComponent>(b);
    sp.texture_key = "atlas/hero";
    sp.size = {64.0f, 48.0f};
    sp.color = {0.5f, 0.6f, 0.7f, 1.0f};
    sp.z_order = 5;

    auto json = scene_io::to_json(s);

    Scene loaded;
    REQUIRE(scene_io::from_json(loaded, json));

    // Find the child by name.
    entt::entity found = entt::null;
    loaded.for_each([&](entt::entity e) {
        if (loaded.registry().get<Name>(e).value == "Child")
            found = e;
    });
    REQUIRE(loaded.registry().valid(found));
    const auto& lt = loaded.registry().get<Transform2D>(found);
    REQUIRE(lt.position.x == 42.0f);
    REQUIRE(lt.position.y == -7.0f);
    REQUIRE(lt.rotation == 1.5f);
    const auto& ls = loaded.registry().get<SpriteComponent>(found);
    REQUIRE(ls.texture_key == "atlas/hero");
    REQUIRE(ls.size.x == 64.0f);
    REQUIRE(ls.z_order == 5);
}

TEST_CASE("Scene: nodes get unique stable uuids") {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B");
    const auto& ida = s.registry().get<Id>(a).uuid;
    const auto& idb = s.registry().get<Id>(b).uuid;
    REQUIRE(ida.valid());
    REQUIRE(idb.valid());
    REQUIRE_FALSE(ida == idb);
    REQUIRE(s.find_by_uuid(ida) == a);
    REQUIRE(s.find_by_uuid(idb) == b);
    REQUIRE((s.find_by_uuid(Uuid{}) == entt::null));
}

TEST_CASE("Scene: uuids survive a save/load round-trip") {
    Scene s;
    auto a = s.create_node("Parent");
    auto b = s.create_node("Child", a);
    const Uuid ida = s.registry().get<Id>(a).uuid;
    const Uuid idb = s.registry().get<Id>(b).uuid;

    Scene loaded;
    REQUIRE(scene_io::from_json(loaded, scene_io::to_json(s)));

    auto la = loaded.find_by_uuid(ida);
    auto lb = loaded.find_by_uuid(idb);
    REQUIRE(loaded.registry().valid(la));
    REQUIRE(loaded.registry().valid(lb));
    REQUIRE(loaded.registry().get<Name>(la).value == "Parent");
    // Parent linkage is preserved by uuid, not by load order.
    REQUIRE(loaded.registry().get<Hierarchy>(lb).parent == la);
}

TEST_CASE("Scene: merging two loads keeps references distinct") {
    Scene src;
    auto n = src.create_node("Shared");
    const Uuid id = src.registry().get<Id>(n).uuid;
    const auto json = scene_io::to_json(src);

    // Loading the same file twice into one scene appends two independent
    // copies; the first keeps the original uuid, the duplicate is detectable.
    Scene merged;
    REQUIRE(scene_io::from_json(merged, json));
    REQUIRE(scene_io::from_json(merged, json));

    int shared_nodes = 0;
    merged.for_each([&](entt::entity e) {
        if (e != merged.root() && merged.registry().get<Name>(e).value == "Shared")
            ++shared_nodes;
    });
    REQUIRE(shared_nodes == 2);
    // The original uuid still resolves to exactly one node.
    REQUIRE(merged.registry().valid(merged.find_by_uuid(id)));
}
