// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <string>

#include <gtest/gtest.h>

#include "engine/core/Expected.hpp"

using namespace vaxelis;

// Expected.hpp aliases std::expected when the standard library has it and falls
// back to a shim otherwise. Every CI toolchain takes the std path, so these
// tests pin the API surface the RHI depends on; on a stdlib without
// std::expected the same tests are what covers the shim.

namespace {
enum class Err { Bad, Worse };
}

TEST(Expected, CarriesAValue) {
    expected<int, Err> e = 7;
    EXPECT_TRUE(e.has_value());
    EXPECT_TRUE(static_cast<bool>(e));
    EXPECT_EQ(*e, 7);
    EXPECT_EQ(e.value_or(99), 7);
}

TEST(Expected, CarriesAnError) {
    expected<int, Err> e = unexpected<Err>(Err::Bad);
    EXPECT_FALSE(e.has_value());
    EXPECT_FALSE(static_cast<bool>(e));
    EXPECT_EQ(e.error(), Err::Bad);
    // The fallback is what every RHI caller uses to keep going after a failure.
    EXPECT_EQ(e.value_or(99), 99);
}

TEST(Expected, SupportsNonTrivialValueTypes) {
    expected<std::string, Err> e = std::string("texture");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(*e, "texture");
    EXPECT_EQ(e->size(), 7u);
}

TEST(Expected, AndThenRunsOnlyOnSuccess) {
    const auto twice = [](int v) { return expected<int, Err>(v * 2); };

    expected<int, Err> ok = 21;
    EXPECT_EQ(*ok.and_then(twice), 42);

    expected<int, Err> bad = unexpected<Err>(Err::Worse);
    const auto chained = bad.and_then(twice);
    ASSERT_FALSE(chained.has_value());
    EXPECT_EQ(chained.error(), Err::Worse); // error passes straight through
}

TEST(Expected, TransformErrorRemapsOnlyTheErrorType) {
    const auto name = [](Err) { return std::string("failed"); };

    expected<int, Err> bad = unexpected<Err>(Err::Bad);
    const auto renamed = bad.transform_error(name);
    ASSERT_FALSE(renamed.has_value());
    EXPECT_EQ(renamed.error(), "failed");

    expected<int, Err> ok = 5;
    const auto untouched = ok.transform_error(name);
    ASSERT_TRUE(untouched.has_value());
    EXPECT_EQ(*untouched, 5);
}
