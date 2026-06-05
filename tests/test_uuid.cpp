#include <catch2/catch_test_macros.hpp>

#include <unordered_set>

#include "engine/core/Uuid.hpp"

using namespace vaxelis;

TEST_CASE("Uuid: default is null, generated is not") {
    REQUIRE_FALSE(Uuid{}.valid());
    REQUIRE(generate_uuid().valid());
}

TEST_CASE("Uuid: canonical text round-trips") {
    auto u = generate_uuid();
    auto text = to_string(u);
    REQUIRE(text.size() == 36); // 32 hex + 4 dashes
    REQUIRE(uuid_from_string(text) == u);
}

TEST_CASE("Uuid: dashes are optional when parsing") {
    auto u = generate_uuid();
    std::string compact;
    for (char c : to_string(u))
        if (c != '-')
            compact.push_back(c);
    REQUIRE(uuid_from_string(compact) == u);
}

TEST_CASE("Uuid: malformed text parses to null") {
    REQUIRE_FALSE(uuid_from_string("not-a-uuid").valid());
    REQUIRE_FALSE(uuid_from_string("").valid());
    REQUIRE_FALSE(uuid_from_string("zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz").valid());
    // One nibble short.
    REQUIRE_FALSE(uuid_from_string("0000000-0000-0000-0000-000000000000").valid());
}

TEST_CASE("Uuid: generation is collision-free over a batch") {
    std::unordered_set<Uuid> seen;
    for (int i = 0; i < 10000; ++i)
        REQUIRE(seen.insert(generate_uuid()).second);
}
