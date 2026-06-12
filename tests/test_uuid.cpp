#include <gtest/gtest.h>

#include <unordered_set>

#include "engine/core/Uuid.hpp"

using namespace vaxelis;

TEST(Uuid, DefaultIsNullGeneratedIsNot) {
    EXPECT_FALSE(Uuid{}.valid());
    EXPECT_TRUE(generate_uuid().valid());
}

TEST(Uuid, CanonicalTextRoundTrips) {
    auto u = generate_uuid();
    auto text = to_string(u);
    EXPECT_EQ(text.size(), 36u); // 32 hex + 4 dashes
    EXPECT_EQ(uuid_from_string(text), u);
}

TEST(Uuid, DashesAreOptionalWhenParsing) {
    auto u = generate_uuid();
    std::string compact;
    for (char c : to_string(u))
        if (c != '-')
            compact.push_back(c);
    EXPECT_EQ(uuid_from_string(compact), u);
}

TEST(Uuid, MalformedTextParsesToNull) {
    EXPECT_FALSE(uuid_from_string("not-a-uuid").valid());
    EXPECT_FALSE(uuid_from_string("").valid());
    EXPECT_FALSE(uuid_from_string("zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz").valid());
    // One nibble short.
    EXPECT_FALSE(uuid_from_string("0000000-0000-0000-0000-000000000000").valid());
}

TEST(Uuid, GenerationIsCollisionFreeOverABatch) {
    std::unordered_set<Uuid> seen;
    for (int i = 0; i < 10000; ++i)
        EXPECT_TRUE(seen.insert(generate_uuid()).second);
}
