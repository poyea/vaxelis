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

namespace {

struct Pos {
    float x{0.0f};
    float y{0.0f};
};

struct Vel {
    float dx{0.0f};
};

struct Health {
    int hp{100};
};

} // namespace

TEST(World, AddingAComponentMigratesToANewArchetype) {
    World w;
    const Entity e = w.create();
    EXPECT_FALSE(w.has<Pos>(e));
    EXPECT_EQ(w.archetype_count(), 1u); // just the empty one

    Pos* p = w.add<Pos>(e, Pos{1.0f, 2.0f});
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(w.has<Pos>(e));
    EXPECT_FLOAT_EQ(p->x, 1.0f);
    EXPECT_EQ(w.archetype_count(), 2u); // empty, plus {Pos}
    EXPECT_EQ(w.size(), 1u);
}

TEST(World, AddingASecondComponentKeepsTheFirstsValue) {
    World w;
    const Entity e = w.create();
    w.add<Pos>(e, Pos{3.0f, 4.0f});
    w.add<Vel>(e, Vel{5.0f});

    // The row moved {Pos} -> {Pos, Vel}; Pos must have travelled with it.
    ASSERT_TRUE(w.has<Pos>(e));
    ASSERT_TRUE(w.has<Vel>(e));
    EXPECT_FLOAT_EQ(w.try_get<Pos>(e)->x, 3.0f);
    EXPECT_FLOAT_EQ(w.try_get<Pos>(e)->y, 4.0f);
    EXPECT_FLOAT_EQ(w.try_get<Vel>(e)->dx, 5.0f);
}

TEST(World, AddingAComponentTwiceOverwritesInPlace) {
    World w;
    const Entity e = w.create();
    w.add<Health>(e, Health{50});
    const size_t shapes = w.archetype_count();

    w.add<Health>(e, Health{75});
    EXPECT_EQ(w.try_get<Health>(e)->hp, 75);
    EXPECT_EQ(w.archetype_count(), shapes); // no new shape, so no migration
}

TEST(World, RemovingAComponentKeepsTheOthers) {
    World w;
    const Entity e = w.create();
    w.add<Pos>(e, Pos{7.0f, 8.0f});
    w.add<Vel>(e, Vel{9.0f});

    w.remove<Vel>(e);
    EXPECT_FALSE(w.has<Vel>(e));
    EXPECT_EQ(w.try_get<Vel>(e), nullptr);
    ASSERT_TRUE(w.has<Pos>(e));
    EXPECT_FLOAT_EQ(w.try_get<Pos>(e)->x, 7.0f);

    // Removing something the entity does not have is a no-op.
    w.remove<Vel>(e);
    EXPECT_TRUE(w.has<Pos>(e));
}

TEST(World, EntitiesOfTheSameShapeShareOneArchetype) {
    World w;
    for (int i = 0; i < 8; ++i) {
        const Entity e = w.create();
        w.add<Pos>(e, Pos{static_cast<float>(i), 0.0f});
    }
    // empty + {Pos}, however many entities pass through it.
    EXPECT_EQ(w.archetype_count(), 2u);
    EXPECT_EQ(w.size(), 8u);
}

TEST(World, MigratingOneEntityLeavesTheOthersIntact) {
    World w;
    std::vector<Entity> entities;
    for (int i = 0; i < 8; ++i) {
        entities.push_back(w.create());
        w.add<Pos>(entities.back(), Pos{static_cast<float>(i), 0.0f});
    }

    // Pulling an entity out of the middle of {Pos} swaps the last row into its
    // place; every other entity must still read back its own value.
    w.add<Vel>(entities[2], Vel{1.0f});

    for (size_t i = 0; i < entities.size(); ++i) {
        const Pos* p = w.try_get<Pos>(entities[i]);
        ASSERT_NE(p, nullptr) << "entity " << i;
        EXPECT_FLOAT_EQ(p->x, static_cast<float>(i)) << "entity " << i;
    }
    EXPECT_TRUE(w.has<Vel>(entities[2]));
    EXPECT_FALSE(w.has<Vel>(entities[7]));
}

TEST(World, StaleHandlesAreRejectedNotFollowed) {
    World w;
    const Entity e = w.create();
    w.add<Pos>(e, Pos{1.0f, 1.0f});
    w.destroy(e);

    EXPECT_FALSE(w.has<Pos>(e));
    EXPECT_EQ(w.try_get<Pos>(e), nullptr);
    EXPECT_EQ(w.add<Pos>(e), nullptr);
    w.remove<Pos>(e); // must not crash
}
