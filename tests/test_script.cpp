// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "engine/input/Input.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/Script.hpp"

using namespace vaxelis;

namespace {
// Writes a tiny script (defines on_update so the host binds an instance table)
// to a uniquely-named file under the temp dir and returns its path.
std::string write_temp_script(const char* tag) {
    const auto path =
        std::filesystem::temp_directory_path() / ("vaxelis-test-" + std::string(tag) + ".lua");
    std::ofstream f(path);
    f << "function on_update(dt) end\n";
    f.close();
    return path.string();
}
} // namespace

TEST(ScriptHost, DestroyingAnEntityTearsDownItsLuaInstance) {
    Scene s;
    Input in;
    ScriptHost host;
    ASSERT_TRUE(host.init(s, in));
    host.register_with(s);

    auto e = s.create_node("Scripted");
    auto& sc = s.registry().emplace<ScriptComponent>(e);
    sc.path = write_temp_script("destroy");

    host.update(1.0f / 60.0f, s);
    EXPECT_EQ(host.instance_count(), 1u);

    const std::string key = s.registry().get<ScriptComponent>(e).instance_key;
    EXPECT_FALSE(key.empty());
    EXPECT_TRUE(host.has_instance(key));

    s.destroy_node(e);

    // The Lua subtable and host bookkeeping must be gone. (We assert via the
    // host's accessors rather than indexing host.lua() directly; sol::state's
    // operator[] needs sol2's full headers, which this TU deliberately avoids.)
    EXPECT_FALSE(host.has_instance(key));
    EXPECT_EQ(host.instance_count(), 0u);
}

TEST(ScriptHost, ErasingTheScriptComponentTearsDownItsLuaInstance) {
    Scene s;
    Input in;
    ScriptHost host;
    ASSERT_TRUE(host.init(s, in));
    host.register_with(s);

    auto e = s.create_node("Scripted");
    s.registry().emplace<ScriptComponent>(e).path = write_temp_script("erase");

    host.update(1.0f / 60.0f, s);
    const std::string key = s.registry().get<ScriptComponent>(e).instance_key;
    EXPECT_TRUE(host.has_instance(key));

    s.registry().remove<ScriptComponent>(e);

    EXPECT_FALSE(host.has_instance(key));
    EXPECT_EQ(host.instance_count(), 0u);
}
