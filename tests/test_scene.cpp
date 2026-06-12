#include <gtest/gtest.h>

#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneSerializer.hpp"

using namespace vaxelis;

TEST(Scene, CreateDestroyNodesAndHierarchy) {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    auto c = s.create_node("C", b);

    EXPECT_EQ(s.registry().get<Hierarchy>(a).parent, s.root());
    EXPECT_EQ(s.registry().get<Hierarchy>(b).parent, a);
    EXPECT_EQ(s.registry().get<Hierarchy>(c).parent, b);
    EXPECT_EQ(s.registry().get<Hierarchy>(a).children.size(), 1u);

    // Cascades to b and c.
    s.destroy_node(a);
    EXPECT_FALSE(s.registry().valid(a));
    EXPECT_FALSE(s.registry().valid(b));
    EXPECT_FALSE(s.registry().valid(c));
    EXPECT_TRUE(s.registry().get<Hierarchy>(s.root()).children.empty());
}

TEST(Scene, SetParentRejectsCycles) {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    // Reparenting a under b would form a cycle; must be rejected.
    s.set_parent(a, b);
    EXPECT_EQ(s.registry().get<Hierarchy>(a).parent, s.root());
}

TEST(Scene, WorldMatrixComposesParentTransforms) {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B", a);
    s.registry().get<Transform2D>(a).position = {10.0f, 0.0f};
    s.registry().get<Transform2D>(b).position = {5.0f, 0.0f};
    auto w = s.world_matrix(b);
    EXPECT_FLOAT_EQ(w[3].x, 15.0f);
}

TEST(Scene, JsonRoundTripPreservesHierarchyAndComponents) {
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
    ASSERT_TRUE(scene_io::from_json(loaded, json));

    // Find the child by name.
    entt::entity found = entt::null;
    loaded.for_each([&](entt::entity e) {
        if (loaded.registry().get<Name>(e).value == "Child")
            found = e;
    });
    ASSERT_TRUE(loaded.registry().valid(found));
    const auto& lt = loaded.registry().get<Transform2D>(found);
    EXPECT_FLOAT_EQ(lt.position.x, 42.0f);
    EXPECT_FLOAT_EQ(lt.position.y, -7.0f);
    EXPECT_FLOAT_EQ(lt.rotation, 1.5f);
    const auto& ls = loaded.registry().get<SpriteComponent>(found);
    EXPECT_EQ(ls.texture_key, "atlas/hero");
    EXPECT_FLOAT_EQ(ls.size.x, 64.0f);
    EXPECT_EQ(ls.z_order, 5);
}

TEST(Scene, NodesGetUniqueStableUuids) {
    Scene s;
    auto a = s.create_node("A");
    auto b = s.create_node("B");
    const auto& ida = s.registry().get<Id>(a).uuid;
    const auto& idb = s.registry().get<Id>(b).uuid;
    EXPECT_TRUE(ida.valid());
    EXPECT_TRUE(idb.valid());
    EXPECT_FALSE(ida == idb);
    EXPECT_EQ(s.find_by_uuid(ida), a);
    EXPECT_EQ(s.find_by_uuid(idb), b);
    EXPECT_TRUE(s.find_by_uuid(Uuid{}) == entt::null);
}

TEST(Scene, UuidsSurviveASaveLoadRoundTrip) {
    Scene s;
    auto a = s.create_node("Parent");
    auto b = s.create_node("Child", a);
    const Uuid ida = s.registry().get<Id>(a).uuid;
    const Uuid idb = s.registry().get<Id>(b).uuid;

    Scene loaded;
    ASSERT_TRUE(scene_io::from_json(loaded, scene_io::to_json(s)));

    auto la = loaded.find_by_uuid(ida);
    auto lb = loaded.find_by_uuid(idb);
    ASSERT_TRUE(loaded.registry().valid(la));
    ASSERT_TRUE(loaded.registry().valid(lb));
    EXPECT_EQ(loaded.registry().get<Name>(la).value, "Parent");
    // Parent linkage is preserved by uuid, not by load order.
    EXPECT_EQ(loaded.registry().get<Hierarchy>(lb).parent, la);
}

TEST(Scene, MergingTwoLoadsKeepsReferencesDistinct) {
    Scene src;
    auto n = src.create_node("Shared");
    const Uuid id = src.registry().get<Id>(n).uuid;
    const auto json = scene_io::to_json(src);

    // Loading the same file twice into one scene appends two independent
    // copies; the first keeps the original uuid, the duplicate is detectable.
    Scene merged;
    ASSERT_TRUE(scene_io::from_json(merged, json));
    ASSERT_TRUE(scene_io::from_json(merged, json));

    int shared_nodes = 0;
    merged.for_each([&](entt::entity e) {
        if (e != merged.root() && merged.registry().get<Name>(e).value == "Shared")
            ++shared_nodes;
    });
    EXPECT_EQ(shared_nodes, 2);
    // The original uuid still resolves to exactly one node.
    EXPECT_TRUE(merged.registry().valid(merged.find_by_uuid(id)));
}
