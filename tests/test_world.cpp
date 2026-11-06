// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <utility>
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

TEST(World, EachVisitsOnlyEntitiesCarryingEveryComponent) {
    World w;
    const Entity pos_only = w.create();
    w.add<Pos>(pos_only, Pos{1.0f, 0.0f});

    const Entity both = w.create();
    w.add<Pos>(both, Pos{2.0f, 0.0f});
    w.add<Vel>(both, Vel{10.0f});

    const Entity vel_only = w.create();
    w.add<Vel>(vel_only, Vel{99.0f});

    int visits = 0;
    float seen_x = 0.0f;
    w.each<Pos, Vel>([&](Pos& p, Vel& v) {
        ++visits;
        seen_x = p.x;
        v.dx += 1.0f;
    });

    EXPECT_EQ(visits, 1); // only `both` has the pair
    EXPECT_FLOAT_EQ(seen_x, 2.0f);
    EXPECT_FLOAT_EQ(w.try_get<Vel>(both)->dx, 11.0f); // writes land in storage
    EXPECT_FLOAT_EQ(w.try_get<Vel>(vel_only)->dx, 99.0f);
}

TEST(World, EachSpansEveryArchetypeThatContainsTheQuery) {
    World w;
    // Same query component, three different shapes: {Pos}, {Pos,Vel},
    // {Pos,Health}. All three must be visited.
    const Entity a = w.create();
    w.add<Pos>(a, Pos{1.0f, 0.0f});
    const Entity b = w.create();
    w.add<Pos>(b, Pos{2.0f, 0.0f});
    w.add<Vel>(b, Vel{0.0f});
    const Entity c = w.create();
    w.add<Pos>(c, Pos{3.0f, 0.0f});
    w.add<Health>(c, Health{1});

    float total = 0.0f;
    int visits = 0;
    w.each<Pos>([&](Pos& p) {
        total += p.x;
        ++visits;
    });

    EXPECT_EQ(visits, 3);
    EXPECT_FLOAT_EQ(total, 6.0f);
    EXPECT_EQ(w.archetype_count(), 4u); // empty, {Pos}, {Pos,Vel}, {Pos,Health}
}

TEST(World, EachEntityHandsBackUsableHandles) {
    World w;
    for (int i = 0; i < 5; ++i)
        w.add<Health>(w.create(), Health{i});

    std::vector<Entity> seen;
    w.each_entity<Health>([&](Entity e, Health& h) {
        seen.push_back(e);
        h.hp += 100;
    });

    ASSERT_EQ(seen.size(), 5u);
    for (const Entity e : seen) {
        EXPECT_TRUE(w.alive(e));
        ASSERT_NE(w.try_get<Health>(e), nullptr);
        EXPECT_GE(w.try_get<Health>(e)->hp, 100);
    }
}

TEST(World, EachSkipsEmptiedArchetypes) {
    World w;
    const Entity e = w.create();
    w.add<Pos>(e, Pos{1.0f, 0.0f});
    w.destroy(e);

    // The {Pos} archetype still exists but holds no rows.
    int visits = 0;
    w.each<Pos>([&](Pos&) { ++visits; });
    EXPECT_EQ(visits, 0);
    EXPECT_EQ(w.archetype_count(), 2u);
}

TEST(World, QualifiedComponentTypesNameTheSameComponent) {
    World w;
    const Entity e = w.create();
    w.add<Pos>(e, Pos{5.0f, 0.0f});

    // const Pos must resolve to the same id as Pos, or the query mask holds a
    // bit no archetype carries and the loop silently visits nothing.
    EXPECT_EQ(component_id<Pos>(), component_id<const Pos>());
    EXPECT_EQ(component_id<Pos>(), component_id<Pos&>());
    EXPECT_TRUE(w.has<const Pos>(e));

    int visits = 0;
    w.each<const Pos>([&](const Pos& p) {
        ++visits;
        EXPECT_FLOAT_EQ(p.x, 5.0f);
    });
    EXPECT_EQ(visits, 1);
}

TEST(World, ConstWorldSupportsReadOnlyQueries) {
    World w;
    const Entity e = w.create();
    w.add<Pos>(e, Pos{2.0f, 3.0f});

    const World& ro = w;
    ASSERT_NE(ro.try_get<Pos>(e), nullptr);
    EXPECT_FLOAT_EQ(ro.try_get<Pos>(e)->y, 3.0f);

    int visits = 0;
    ro.each<Pos>([&](const Pos& p) {
        ++visits;
        EXPECT_FLOAT_EQ(p.x, 2.0f);
    });
    EXPECT_EQ(visits, 1);

    ro.each_entity<Pos>([&](Entity owner, const Pos&) { EXPECT_TRUE(ro.alive(owner)); });
}

namespace {

/// Counts live instances so a leak or double-destroy in the migration paths
/// shows up as a non-zero balance.
struct Counted {
    static inline int alive = 0;
    int value{0};

    Counted() { ++alive; }
    /// User-declared constructors make this a non-aggregate, so the value
    /// constructor has to be spelled out for `Counted{n}` to compile.
    explicit Counted(int v) : value(v) { ++alive; }
    Counted(const Counted& other) : value(other.value) { ++alive; }
    Counted(Counted&& other) noexcept : value(other.value) { ++alive; }
    Counted& operator=(const Counted&) = default;
    Counted& operator=(Counted&&) noexcept = default;
    ~Counted() { --alive; }
};

} // namespace

TEST(World, MigrationsDestroyExactlyWhatTheyCreate) {
    Counted::alive = 0;
    {
        World w;
        const Entity e = w.create();

        // {} -> {Counted} -> {Counted, Pos}
        w.add<Counted>(e, Counted{1});
        w.add<Pos>(e, Pos{1.0f, 0.0f});
        EXPECT_EQ(Counted::alive, 1);

        w.remove<Pos>(e); // migrate back; Counted rides along, still one copy
        EXPECT_EQ(Counted::alive, 1);
        EXPECT_EQ(w.try_get<Counted>(e)->value, 1);

        w.remove<Counted>(e); // dropped entirely
        EXPECT_EQ(Counted::alive, 0);
        EXPECT_FALSE(w.has<Counted>(e));
    }
    EXPECT_EQ(Counted::alive, 0);
}

TEST(World, DestroyingAnEntityRunsItsComponentDestructors) {
    Counted::alive = 0;
    {
        World w;
        std::vector<Entity> entities;
        for (int i = 0; i < 12; ++i) {
            entities.push_back(w.create());
            w.add<Counted>(entities.back(), Counted{i});
        }
        EXPECT_EQ(Counted::alive, 12);

        w.destroy(entities[5]); // swap-and-pop from the middle
        EXPECT_EQ(Counted::alive, 11);
        w.destroy(entities.back()); // and from the end
        EXPECT_EQ(Counted::alive, 10);
    }
    EXPECT_EQ(Counted::alive, 0); // the World destructor takes the rest
}

TEST(World, MigrationSurvivesColumnGrowth) {
    Counted::alive = 0;
    {
        World w;
        std::vector<Entity> entities;
        // Past the initial capacity of 8, so {Counted} reallocates its column
        // and any stale pointer held across the growth would show up here.
        for (int i = 0; i < 40; ++i) {
            entities.push_back(w.create());
            w.add<Counted>(entities.back(), Counted{i});
        }
        EXPECT_EQ(Counted::alive, 40);

        // Migrate the last row, exercising unseat's "nothing moved" branch.
        w.add<Pos>(entities.back(), Pos{9.0f, 0.0f});
        // And one from the middle, exercising the swap.
        w.add<Pos>(entities[10], Pos{10.0f, 0.0f});
        EXPECT_EQ(Counted::alive, 40);

        for (int i = 0; i < 40; ++i) {
            const Counted* c = w.try_get<Counted>(entities[static_cast<size_t>(i)]);
            ASSERT_NE(c, nullptr) << "entity " << i;
            EXPECT_EQ(c->value, i) << "entity " << i;
        }
    }
    EXPECT_EQ(Counted::alive, 0);
}

TEST(World, RemovingTheLastComponentReturnsToTheEmptyArchetype) {
    World w;
    const Entity e = w.create();
    w.add<Pos>(e, Pos{1.0f, 0.0f});
    const size_t shapes = w.archetype_count();

    w.remove<Pos>(e);
    EXPECT_FALSE(w.has<Pos>(e));
    EXPECT_TRUE(w.alive(e));
    // Back in the archetype it started in; no duplicate empty table minted.
    EXPECT_EQ(w.archetype_count(), shapes);
}

TEST(World, VariadicCreateSeatsDirectlyWithoutMigrating) {
    World w;
    const Entity e = w.create(Pos{1.0f, 2.0f}, Vel{3.0f});

    EXPECT_TRUE(w.alive(e));
    ASSERT_TRUE(w.has<Pos>(e));
    ASSERT_TRUE(w.has<Vel>(e));
    EXPECT_FLOAT_EQ(w.try_get<Pos>(e)->x, 1.0f);
    EXPECT_FLOAT_EQ(w.try_get<Pos>(e)->y, 2.0f);
    EXPECT_FLOAT_EQ(w.try_get<Vel>(e)->dx, 3.0f);

    // The proof that nothing migrated: building this incrementally would have
    // created a {Pos} archetype on the way to {Pos, Vel}. Only the empty one
    // and the destination exist.
    EXPECT_EQ(w.archetype_count(), 2u);
}

TEST(World, VariadicCreateAgreesWithIncrementalConstruction) {
    World w;
    const Entity built = w.create();
    w.add<Pos>(built, Pos{5.0f, 6.0f});
    w.add<Vel>(built, Vel{7.0f});
    const size_t shapes = w.archetype_count();

    const Entity direct = w.create(Pos{5.0f, 6.0f}, Vel{7.0f});

    EXPECT_FLOAT_EQ(w.try_get<Pos>(direct)->x, w.try_get<Pos>(built)->x);
    EXPECT_FLOAT_EQ(w.try_get<Vel>(direct)->dx, w.try_get<Vel>(built)->dx);
    // Same shape, so it reuses the archetype rather than minting another.
    EXPECT_EQ(w.archetype_count(), shapes);
    EXPECT_EQ(w.size(), 2u);
}

TEST(World, MoveConstructionCarriesEntitiesAndComponents) {
    World src;
    const Entity e = src.create(Pos{1.0f, 2.0f}, Vel{3.0f});
    const Entity bare = src.create();
    const size_t shapes = src.archetype_count();

    World dst(std::move(src));

    // Records hold Table*, and the tables live behind unique_ptr, so the move
    // must not have disturbed either the handles or the component values.
    EXPECT_TRUE(dst.alive(e));
    EXPECT_TRUE(dst.alive(bare));
    EXPECT_EQ(dst.size(), 2u);
    EXPECT_EQ(dst.archetype_count(), shapes);
    EXPECT_FLOAT_EQ(dst.try_get<Pos>(e)->x, 1.0f);
    EXPECT_FLOAT_EQ(dst.try_get<Pos>(e)->y, 2.0f);
    EXPECT_FLOAT_EQ(dst.try_get<Vel>(e)->dx, 3.0f);

    // Structural change after the move still works, which exercises the
    // migration path against the moved tables.
    dst.add<Health>(e, Health{42});
    EXPECT_EQ(dst.try_get<Health>(e)->hp, 42);
    EXPECT_FLOAT_EQ(dst.try_get<Pos>(e)->x, 1.0f);
}

TEST(World, MoveAssignmentReplacesTheDestination) {
    World dst;
    dst.create(Pos{9.0f, 9.0f});

    World src;
    const Entity kept = src.create(Pos{4.0f, 5.0f});

    dst = std::move(src);

    EXPECT_TRUE(dst.alive(kept));
    EXPECT_EQ(dst.size(), 1u);
    EXPECT_FLOAT_EQ(dst.try_get<Pos>(kept)->x, 4.0f);
    EXPECT_FLOAT_EQ(dst.try_get<Pos>(kept)->y, 5.0f);
}

TEST(World, MovedFromWorldIsStillUsable) {
    World src;
    src.create(Pos{1.0f, 1.0f});
    World dst(std::move(src));

    // Not merely destructible: the source has to behave like a fresh world,
    // because anything embedding a World is reassigned by value.
    EXPECT_EQ(src.size(), 0u);

    const Entity e = src.create(Pos{7.0f, 8.0f});
    EXPECT_TRUE(src.alive(e));
    EXPECT_EQ(src.size(), 1u);
    EXPECT_FLOAT_EQ(src.try_get<Pos>(e)->x, 7.0f);

    // And the world it fed is untouched by that.
    EXPECT_EQ(dst.size(), 1u);
}

TEST(World, MoveAssignmentDestroysTheComponentsItReplaces) {
    Counted::alive = 0;
    {
        World dst;
        for (int i = 0; i < 8; ++i)
            dst.create(Counted{i});
        EXPECT_EQ(Counted::alive, 8);

        World src;
        src.create(Counted{99});
        EXPECT_EQ(Counted::alive, 9);

        // Assigning over dst releases its tables, which runs the destructors of
        // the eight it held; only src's one survives.
        dst = std::move(src);
        EXPECT_EQ(Counted::alive, 1);
    }
    EXPECT_EQ(Counted::alive, 0);
}

TEST(World, VariadicCreateDestroysWhatItBuilt) {
    Counted::alive = 0;
    {
        World w;
        for (int i = 0; i < 32; ++i)
            w.create(Counted{i}, Pos{static_cast<float>(i), 0.0f});
        EXPECT_EQ(Counted::alive, 32);

        w.destroy(w.create(Counted{99}));
        EXPECT_EQ(Counted::alive, 32);
    }
    EXPECT_EQ(Counted::alive, 0);
}

namespace {

/// Subscriber for the signal tests. Member functions, because that is the
/// shape Sink::connect takes and the one the engine's subsystems use.
struct Watcher {
    int constructed{0};
    int destroyed{0};
    float value_at_destroy{-1.0f};
    size_t size_at_destroy{0};

    void on_pos_constructed(World&, Entity) { ++constructed; }

    void on_pos_destroyed(World& w, Entity e) {
        ++destroyed;
        // The contract is that this runs before the data goes.
        if (const Pos* p = w.try_get<Pos>(e))
            value_at_destroy = p->x;
        size_at_destroy = w.size();
    }
};

} // namespace

TEST(WorldSignals, ConstructFiresForAddAndVariadicCreate) {
    World w;
    Watcher watcher;
    w.on_construct<Pos>().connect<&Watcher::on_pos_constructed>(watcher);

    w.add<Pos>(w.create(), Pos{1.0f, 0.0f});
    EXPECT_EQ(watcher.constructed, 1);

    w.create(Pos{2.0f, 0.0f}, Vel{1.0f});
    EXPECT_EQ(watcher.constructed, 2);

    // A second add over an existing Pos is an overwrite, not a construction.
    const Entity e = w.create(Pos{3.0f, 0.0f});
    EXPECT_EQ(watcher.constructed, 3);
    w.add<Pos>(e, Pos{4.0f, 0.0f});
    EXPECT_EQ(watcher.constructed, 3);

    // A component nobody subscribed to publishes nothing.
    w.add<Health>(e, Health{1});
    EXPECT_EQ(watcher.constructed, 3);
}

TEST(WorldSignals, DestroyFiresWhileTheComponentIsStillReadable) {
    World w;
    Watcher watcher;
    w.on_destroy<Pos>().connect<&Watcher::on_pos_destroyed>(watcher);

    const Entity e = w.create(Pos{7.0f, 0.0f});
    w.remove<Pos>(e);
    EXPECT_EQ(watcher.destroyed, 1);
    EXPECT_FLOAT_EQ(watcher.value_at_destroy, 7.0f);
    EXPECT_FALSE(w.has<Pos>(e));

    // Taking the whole entity publishes for each component it carried, and the
    // entity is still counted as live at that point.
    const Entity whole = w.create(Pos{9.0f, 0.0f}, Vel{1.0f});
    const size_t live_before = w.size();
    w.destroy(whole);
    EXPECT_EQ(watcher.destroyed, 2);
    EXPECT_FLOAT_EQ(watcher.value_at_destroy, 9.0f);
    EXPECT_EQ(watcher.size_at_destroy, live_before);
}

TEST(WorldSignals, DisconnectStopsDelivery) {
    World w;
    Watcher by_member;
    Watcher by_instance;
    w.on_construct<Pos>().connect<&Watcher::on_pos_constructed>(by_member);
    w.on_construct<Pos>().connect<&Watcher::on_pos_constructed>(by_instance);

    w.create(Pos{1.0f, 0.0f});
    EXPECT_EQ(by_member.constructed, 1);
    EXPECT_EQ(by_instance.constructed, 1);

    w.on_construct<Pos>().disconnect<&Watcher::on_pos_constructed>(by_member);
    w.create(Pos{2.0f, 0.0f});
    EXPECT_EQ(by_member.constructed, 1);
    EXPECT_EQ(by_instance.constructed, 2);

    // The by-instance overload drops every subscription naming it.
    w.on_construct<Pos>().disconnect(&by_instance);
    w.create(Pos{3.0f, 0.0f});
    EXPECT_EQ(by_instance.constructed, 2);
}

TEST(WorldSignals, SubscriptionsSurviveAWorldMove) {
    World src;
    Watcher watcher;
    src.on_construct<Pos>().connect<&Watcher::on_pos_constructed>(watcher);
    src.on_destroy<Pos>().connect<&Watcher::on_pos_destroyed>(watcher);

    World dst(std::move(src));
    const Entity e = dst.create(Pos{5.0f, 0.0f});
    EXPECT_EQ(watcher.constructed, 1);

    dst.destroy(e);
    EXPECT_EQ(watcher.destroyed, 1);
}

TEST(WorldSignals, TeardownDoesNotPublish) {
    Watcher watcher;
    {
        World w;
        w.on_destroy<Pos>().connect<&Watcher::on_pos_destroyed>(watcher);
        w.create(Pos{1.0f, 0.0f});
        w.create(Pos{2.0f, 0.0f});
    }
    // Matching entt: the world going away is not each entity being destroyed,
    // and a subsystem mid-shutdown does not want the callback.
    EXPECT_EQ(watcher.destroyed, 0);
}

TEST(WorldQueries, QueryTypesIgnoreCvQualifiers) {
    // The port needs this: a query written as each<const Pos> must resolve to
    // the same component as Pos. If component_id did not strip qualifiers, the
    // query mask would name a bit no archetype carries and the walk would
    // silently visit nothing -- passing tests, rendering an empty scene.
    EXPECT_EQ(component_id<const Pos>(), component_id<Pos>());
    EXPECT_EQ(component_id<Pos&>(), component_id<Pos>());
    EXPECT_EQ(component_id<const Pos&>(), component_id<Pos>());

    World w;
    w.create(Pos{1.0f, 0.0f}, Vel{1.0f});
    w.create(Pos{2.0f, 0.0f}, Vel{1.0f});

    int visited = 0;
    float sum = 0.0f;
    w.each<const Pos>([&](const Pos& p) {
        ++visited;
        sum += p.x;
    });
    EXPECT_EQ(visited, 2);
    EXPECT_FLOAT_EQ(sum, 3.0f);

    // Mixed qualification across a multi-component query.
    int pairs = 0;
    w.each<const Pos, Vel>([&](const Pos&, Vel& v) {
        ++pairs;
        v.dx += 1.0f;
    });
    EXPECT_EQ(pairs, 2);
}

TEST(World, CreateCanCloneAComponentFromTheSameArchetype) {
    World w;
    const Entity src = w.create(Pos{1.0f, 2.0f});

    // The argument is a reference into the very column the new row grows, so
    // one of these iterations crosses a reallocation boundary. Reading the
    // argument after seating would read freed memory.
    for (int i = 0; i < 64; ++i) {
        const Entity clone = w.create(*w.try_get<Pos>(src));
        ASSERT_NE(w.try_get<Pos>(clone), nullptr);
        EXPECT_FLOAT_EQ(w.try_get<Pos>(clone)->x, 1.0f);
        EXPECT_FLOAT_EQ(w.try_get<Pos>(clone)->y, 2.0f);
    }
    EXPECT_EQ(w.size(), 65u);
}
