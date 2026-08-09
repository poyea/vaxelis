// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "engine/assets/FileWatcher.hpp"

using namespace vaxelis;

TEST(FileWatcher, FiresCallbackOnMtimeChange) {
    auto dir = std::filesystem::temp_directory_path() / "vaxelis_fw_test";
    std::filesystem::create_directories(dir);
    auto file = dir / "probe.txt";
    {
        std::ofstream f(file);
        f << "v1";
    }

    FileWatcher w;
    // Tick on every call so we can drive the watcher synchronously.
    w.set_interval(0.0f);
    int hits = 0;
    w.watch(file.string(), [&](const std::string&) { ++hits; });

    // First tick records baseline mtime; no callback expected.
    w.tick(1.0f);
    EXPECT_EQ(hits, 0);

    // Sleep past filesystem mtime resolution (~1s on FAT/NTFS in some cases),
    // then rewrite the file.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    {
        std::ofstream f(file);
        f << "v2";
    }
    w.tick(1.0f);
    EXPECT_EQ(hits, 1);

    std::filesystem::remove_all(dir);
}
