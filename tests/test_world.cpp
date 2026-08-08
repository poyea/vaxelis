#include <vector>

#include <gtest/gtest.h>

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/World.hpp"

using namespace vaxelis::ecs;

TEST(World, StartsEmptyWithOnlyTheEmptyArchetype) {
    World w;
    EXPECT_EQ(w.size(), 0u);
    EXPECT_EQ(w.archetype_count(), 1u);
    EXPECT_FALSE(w.alive(kNoEntity));
}

TEST(World, CreateAndDestroyTrackLiveCount) {
    World w;
    const Entity a = w.create();
    const Entity b = w.create();

    EXPECT_TRUE(w.alive(a));
    EXPECT_TRUE(w.alive(b));
    EXPECT_NE(a, b);
    EXPECT_EQ(w.size(), 2u);

    w.destroy(a);
    EXPECT_FALSE(w.alive(a));
    EXPECT_TRUE(w.alive(b));
    EXPECT_EQ(w.size(), 1u);
}

TEST(World, DestroyingTwiceIsHarmless) {
    World w;
    const Entity e = w.create();
    w.destroy(e);
    w.destroy(e);
    EXPECT_EQ(w.size(), 0u);
    EXPECT_FALSE(w.alive(e));
}

TEST(World, RecycledSlotsDoNotReviveOldHandles) {
    World w;
    const Entity first = w.create();
    w.destroy(first);
    const Entity second = w.create();

    // The slot is reused, which is the whole reason handles carry a generation.
    EXPECT_EQ(second.index, first.index);
    EXPECT_NE(second.generation, first.generation);
    EXPECT_TRUE(w.alive(second));
    EXPECT_FALSE(w.alive(first));
}

TEST(World, DestroyingFromTheMiddleKeepsEveryOtherHandleValid) {
    World w;
    std::vector<Entity> entities;
    for (int i = 0; i < 16; ++i)
        entities.push_back(w.create());

    // Removal swaps the last row into the gap, so the entity that moved must
    // still resolve through its handle afterwards.
    w.destroy(entities[3]);
    w.destroy(entities[0]);
    EXPECT_EQ(w.size(), 14u);

    for (size_t i = 0; i < entities.size(); ++i) {
        const bool removed = (i == 0 || i == 3);
        EXPECT_EQ(w.alive(entities[i]), !removed) << "entity " << i;
    }
}
