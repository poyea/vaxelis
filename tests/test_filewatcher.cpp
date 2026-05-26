#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "engine/assets/FileWatcher.hpp"

using namespace vaxelis;

TEST_CASE("FileWatcher: fires callback on mtime change") {
    auto dir = std::filesystem::temp_directory_path() / "vaxelis_fw_test";
    std::filesystem::create_directories(dir);
    auto file = dir / "probe.txt";
    { std::ofstream f(file); f << "v1"; }

    FileWatcher w;
    w.set_interval(0.0f);  // tick on every call
    int hits = 0;
    w.watch(file.string(), [&](const std::string&) { ++hits; });

    // First tick records baseline mtime; no callback expected.
    w.tick(1.0f);
    REQUIRE(hits == 0);

    // Sleep past filesystem mtime resolution (~1s on FAT/NTFS in some cases),
    // then rewrite the file.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    { std::ofstream f(file); f << "v2"; }
    w.tick(1.0f);
    REQUIRE(hits == 1);

    std::filesystem::remove_all(dir);
}
