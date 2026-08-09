// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "engine/ecs/Archetype.hpp"
#include "engine/ecs/ComponentType.hpp"
#include "engine/ecs/Signature.hpp"

using namespace vaxelis::ecs;

namespace {

struct Position {
    float x{0.0f};
    float y{0.0f};
};

struct Velocity {
    float dx{0.0f};
    float dy{0.0f};
};

struct Tag {
    int value{0};
};

/// Counts live instances, so a leaked or doubly-destroyed row shows up as a
/// non-zero balance at the end of a test.
struct Tracked {
    static inline int alive = 0;
    int value{0};

    Tracked() { ++alive; }
    Tracked(const Tracked& other) : value(other.value) { ++alive; }
    Tracked(Tracked&& other) noexcept : value(other.value) { ++alive; }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&) noexcept = default;
    ~Tracked() { --alive; }
};

std::vector<ComponentId> ids(std::initializer_list<ComponentId> list) {
    return std::vector<ComponentId>(list);
}

} // namespace

TEST(ComponentType, IdsAreStableAndDistinct) {
    const ComponentId pos = component_id<Position>();
    const ComponentId vel = component_id<Velocity>();

    EXPECT_EQ(pos, component_id<Position>()); // same type, same id every time
    EXPECT_NE(pos, vel);
    EXPECT_LT(pos, kMaxComponents);

    const ComponentInfo& info = component_info(pos);
    EXPECT_EQ(info.id, pos);
    EXPECT_EQ(info.size, sizeof(Position));
    EXPECT_EQ(info.alignment, alignof(Position));
    EXPECT_NE(info.default_construct, nullptr);
    EXPECT_NE(info.move_construct, nullptr);
    EXPECT_NE(info.destroy, nullptr);
}

TEST(Signature, TracksMembershipAndSubsets) {
    const auto pos_vel = signature_of<Position, Velocity>();
    const auto pos_only = signature_of<Position>();

    EXPECT_TRUE(pos_vel.test(component_id<Position>()));
    EXPECT_TRUE(pos_vel.test(component_id<Velocity>()));
    EXPECT_FALSE(pos_vel.test(component_id<Tag>()));
    EXPECT_EQ(pos_vel.count(), 2u);

    // A query matches an archetype when the archetype contains the query.
    EXPECT_TRUE(pos_vel.contains(pos_only));
    EXPECT_FALSE(pos_only.contains(pos_vel));
    EXPECT_TRUE(pos_vel.contains(Signature{}));

    Signature s = pos_vel;
    s.reset(component_id<Velocity>());
    EXPECT_EQ(s, pos_only);
    EXPECT_TRUE(Signature{}.empty());
}

TEST(Archetype, DerivesItsSignatureAndIgnoresDuplicates) {
    const ComponentId pos = component_id<Position>();
    Archetype a(ids({pos, component_id<Velocity>(), pos}));

    EXPECT_EQ(a.signature(), (signature_of<Position, Velocity>()));
    EXPECT_EQ(a.component_ids().size(), 2u);
    EXPECT_TRUE(a.has(component_id<Position>()));
    EXPECT_FALSE(a.has(component_id<Tag>()));
    EXPECT_TRUE(a.empty());
}

TEST(Archetype, AddRowDefaultConstructsAndSurvivesGrowth) {
    Archetype a(ids({component_id<Position>(), component_id<Velocity>()}));

    const size_t first = a.add_row();
    EXPECT_EQ(first, 0u);
    EXPECT_FLOAT_EQ(a.get<Position>(first).x, 0.0f); // default-constructed
    a.get<Position>(first) = {1.0f, 2.0f};

    // Push well past the initial capacity so the columns reallocate.
    constexpr int kRows = 200;
    for (int i = 1; i < kRows; ++i) {
        const size_t row = a.add_row();
        a.get<Position>(row).x = static_cast<float>(i);
    }
    ASSERT_EQ(a.size(), static_cast<size_t>(kRows));
    EXPECT_GE(a.capacity(), a.size());

    // Values written before the reallocation are still intact afterwards.
    EXPECT_FLOAT_EQ(a.get<Position>(0).x, 1.0f);
    EXPECT_FLOAT_EQ(a.get<Position>(0).y, 2.0f);
    for (int i = 1; i < kRows; ++i)
        EXPECT_FLOAT_EQ(a.get<Position>(static_cast<size_t>(i)).x, static_cast<float>(i));
}

TEST(Archetype, ColumnIsContiguousAndTyped) {
    Archetype a(ids({component_id<Position>()}));
    for (int i = 0; i < 4; ++i)
        a.get<Position>(a.add_row()).x = static_cast<float>(i);

    const std::span<Position> col = a.column<Position>();
    ASSERT_EQ(col.size(), 4u);
    // Neighbouring rows are neighbouring addresses: that is the whole point.
    EXPECT_EQ(&col[1] - &col[0], 1);
    EXPECT_FLOAT_EQ(col[3].x, 3.0f);

    // A column this archetype does not hold reads back empty rather than
    // dereferencing nothing.
    EXPECT_TRUE(a.column<Tag>().empty());
}

TEST(Archetype, RemoveRowMovesTheLastRowIntoTheGap) {
    Archetype a(ids({component_id<Position>()}));
    for (int i = 0; i < 3; ++i)
        a.get<Position>(a.add_row()).x = static_cast<float>(i);

    const size_t vacated = a.remove_row(0);
    EXPECT_EQ(vacated, 2u); // caller repoints whatever mapped to row 2
    ASSERT_EQ(a.size(), 2u);
    EXPECT_FLOAT_EQ(a.get<Position>(0).x, 2.0f);
    EXPECT_FLOAT_EQ(a.get<Position>(1).x, 1.0f);

    // Removing the last row moves nothing.
    EXPECT_EQ(a.remove_row(1), 1u);
    EXPECT_EQ(a.size(), 1u);
    EXPECT_FLOAT_EQ(a.get<Position>(0).x, 2.0f);
}

TEST(Archetype, MoveRowFromCarriesSharedComponentsAndDefaultsTheRest) {
    Archetype src(ids({component_id<Position>(), component_id<Velocity>()}));
    Archetype dst(ids({component_id<Position>(), component_id<Tag>()}));

    const size_t row = src.add_row();
    src.get<Position>(row) = {7.0f, 8.0f};
    src.get<Velocity>(row) = {1.0f, 1.0f};

    const size_t moved = dst.move_row_from(src, row);
    ASSERT_EQ(dst.size(), 1u);

    // Position is in both, so it crosses over intact.
    EXPECT_FLOAT_EQ(dst.get<Position>(moved).x, 7.0f);
    EXPECT_FLOAT_EQ(dst.get<Position>(moved).y, 8.0f);
    // Tag is new to this archetype, so it starts default-constructed.
    EXPECT_EQ(dst.get<Tag>(moved).value, 0);
    // Velocity is dropped: dst has no column for it.
    EXPECT_FALSE(dst.has(component_id<Velocity>()));
    // The source row is left behind for the caller to remove.
    EXPECT_EQ(src.size(), 1u);
}

TEST(Archetype, DestroysEveryComponentItOwns) {
    Tracked::alive = 0;
    {
        Archetype a(ids({component_id<Tracked>()}));
        for (int i = 0; i < 50; ++i)
            a.get<Tracked>(a.add_row()).value = i;
        EXPECT_EQ(Tracked::alive, 50); // growth must not leak the old buffers

        a.remove_row(0);
        EXPECT_EQ(Tracked::alive, 49);
    }
    EXPECT_EQ(Tracked::alive, 0); // and the destructor cleans up the rest
}
