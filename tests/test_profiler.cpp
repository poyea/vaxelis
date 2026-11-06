// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "engine/debug/Profiler.hpp"

using namespace vaxelis;

namespace {

/// The profiler is a process-wide singleton, so a test that leaves a session
/// running would leak into the next one.
struct ProfilerFixture : ::testing::Test {
    void TearDown() override {
        if (Profiler::instance().recording())
            Profiler::instance().end_session("profiler_test_discard.json");
        std::remove("profiler_test_discard.json");
    }
};

/// Exercises the macro itself from a function body, which is where it is meant
/// to be used; the tests below drive ScopeTimer directly.
void macro_timed_function() {
    VX_PROFILE_FUNCTION();
}

size_t count_occurrences(const std::string& haystack, const std::string& needle) {
    size_t n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

} // namespace

TEST_F(ProfilerFixture, RecordsNothingUntilASessionStarts) {
    Profiler& p = Profiler::instance();
    ASSERT_FALSE(p.recording());

    macro_timed_function();
    p.record("also ignored", 0, 10);

    EXPECT_TRUE(p.events().empty());
    // Zero rather than a clock reading, so a caller cannot mistake it for one.
    EXPECT_EQ(p.now_us(), 0u);
}

TEST_F(ProfilerFixture, ScopeTimerRecordsOneEventPerScope) {
    Profiler& p = Profiler::instance();
    p.begin_session("test");

    {
        const ScopeTimer outer{"outer"};
        {
            const ScopeTimer inner{"inner"};
        }
    }

    ASSERT_EQ(p.events().size(), 2u);
    // Inner closes first, so it is recorded first; the outer must enclose it.
    EXPECT_EQ(p.events()[0].name, "inner");
    EXPECT_EQ(p.events()[1].name, "outer");
    const auto& inner = p.events()[0];
    const auto& outer = p.events()[1];
    EXPECT_GE(inner.start_us, outer.start_us);
    EXPECT_LE(inner.start_us + inner.duration_us, outer.start_us + outer.duration_us);
}

TEST_F(ProfilerFixture, SessionRestartDropsThePreviousEvents) {
    Profiler& p = Profiler::instance();
    p.begin_session("first");
    p.record("from first", 0, 1);
    ASSERT_EQ(p.events().size(), 1u);

    p.begin_session("second");
    EXPECT_TRUE(p.events().empty());
}

TEST_F(ProfilerFixture, JsonCarriesEveryEventAndEscapesNames) {
    Profiler& p = Profiler::instance();
    p.begin_session("json session");
    p.record("plain", 10, 20);
    p.record("quote\"and\\slash", 30, 40);

    const std::string json = p.to_json();
    EXPECT_EQ(count_occurrences(json, "\"ph\":\"X\""), 2u);
    EXPECT_NE(json.find("\"ts\":10"), std::string::npos);
    EXPECT_NE(json.find("\"dur\":20"), std::string::npos);
    EXPECT_NE(json.find("json session"), std::string::npos);
    // A raw quote would make the file unparseable, which is the whole point of
    // escaping: the name must survive as an escape sequence.
    EXPECT_NE(json.find("quote\\\"and\\\\slash"), std::string::npos);
}

TEST_F(ProfilerFixture, EndSessionWritesTheFileAndStopsRecording) {
    Profiler& p = Profiler::instance();
    p.begin_session("written");
    p.record("scope", 1, 2);

    const char* path = "profiler_test_trace.json";
    ASSERT_TRUE(p.end_session(path));
    EXPECT_FALSE(p.recording());

    std::ifstream in{path, std::ios::binary};
    ASSERT_TRUE(in.good());
    std::ostringstream buf;
    buf << in.rdbuf();
    in.close();
    const std::string contents = buf.str();
    EXPECT_NE(contents.find("traceEvents"), std::string::npos);
    EXPECT_NE(contents.find("scope"), std::string::npos);
    std::remove(path);

    // A second end_session has no session to end.
    EXPECT_FALSE(p.end_session(path));
}

TEST_F(ProfilerFixture, EndSessionReportsAnUnwritablePath) {
    Profiler& p = Profiler::instance();
    p.begin_session("unwritable");
    p.record("scope", 1, 2);

    // A directory that does not exist; the open fails rather than the write.
    EXPECT_FALSE(p.end_session("no_such_directory_here/trace.json"));
    // Still ended: a failed write must not leave the session recording, or the
    // next begin_session would warn and the events would pile up.
    EXPECT_FALSE(p.recording());
}

TEST_F(ProfilerFixture, FunctionMacroRecordsUnderTheFunctionName) {
    Profiler& p = Profiler::instance();
    p.begin_session("macro");

    macro_timed_function();

    ASSERT_EQ(p.events().size(), 1u);
    EXPECT_EQ(p.events()[0].name, "macro_timed_function");
}
