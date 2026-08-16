// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "RecordingDevice.hpp"
#include "engine/assets/AssetCache.hpp"
#include "engine/assets/AssetRegistry.hpp"

using namespace vaxelis;
using vaxelis::testing::RecordingDevice;

namespace {

// Stand-in for a real asset handle: trivially copyable, null when id == 0.
struct FakeHandle {
    uint32_t id{0};
    constexpr bool valid() const { return id != 0; }
};

// Counts what the registry asked of the type-specific half.
struct Backing {
    int loads{0};
    int reloads{0};
    std::vector<uint32_t> destroyed;
    uint32_t next_id{1};
    bool load_fails{false};
    bool reload_changes{true};

    AssetRegistry<FakeHandle>::Ops ops() {
        AssetRegistry<FakeHandle>::Ops o;
        o.load = [this](const std::string&) {
            ++loads;
            return load_fails ? FakeHandle{} : FakeHandle{next_id++};
        };
        o.reload = [this](const std::string&, FakeHandle& h) {
            ++reloads;
            if (!reload_changes)
                return false;
            h = FakeHandle{next_id++};
            return true;
        };
        o.destroy = [this](FakeHandle h) { destroyed.push_back(h.id); };
        return o;
    }
};

} // namespace

TEST(AssetRegistry, LoadingTheSamePathTwiceLoadsOnce) {
    Backing b;
    AssetRegistry<FakeHandle> reg;
    reg.init(b.ops());

    const auto first = reg.load("tex.png");
    const auto second = reg.load("tex.png");
    EXPECT_TRUE(first.valid());
    EXPECT_EQ(first.id, second.id);
    EXPECT_EQ(b.loads, 1);
    EXPECT_EQ(reg.size(), 1u);
    EXPECT_TRUE(reg.contains("tex.png"));

    // A distinct key over the same file is a distinct asset.
    const auto aliased = reg.load("tex.png", "alias");
    EXPECT_EQ(b.loads, 2);
    EXPECT_NE(aliased.id, first.id);
    EXPECT_EQ(reg.get("alias").id, aliased.id);

    reg.shutdown();
}

TEST(AssetRegistry, FailedLoadsAreNotCached) {
    Backing b;
    b.load_fails = true;
    AssetRegistry<FakeHandle> reg;
    reg.init(b.ops());

    EXPECT_FALSE(reg.load("missing.png").valid());
    EXPECT_EQ(reg.size(), 0u);
    EXPECT_FALSE(reg.contains("missing.png"));

    // Once the file loads, the key binds normally.
    b.load_fails = false;
    EXPECT_TRUE(reg.load("missing.png").valid());
    EXPECT_EQ(reg.size(), 1u);
    EXPECT_EQ(b.loads, 2);
}

TEST(AssetRegistry, UnknownKeysReadBackNull) {
    Backing b;
    AssetRegistry<FakeHandle> reg;
    reg.init(b.ops());
    EXPECT_FALSE(reg.get("nope").valid());
    EXPECT_FALSE(reg.contains("nope"));
}

TEST(AssetRegistry, AdoptTakesOwnershipAndReplaces) {
    Backing b;
    AssetRegistry<FakeHandle> reg;
    reg.init(b.ops());

    reg.adopt("atlas", FakeHandle{100});
    EXPECT_EQ(reg.get("atlas").id, 100u);
    EXPECT_EQ(b.loads, 0); // adopted assets never run the loader

    // Rebinding the key releases what it held before.
    reg.adopt("atlas", FakeHandle{101});
    EXPECT_EQ(reg.get("atlas").id, 101u);
    EXPECT_EQ(b.destroyed, std::vector<uint32_t>{100});

    // No path behind it, so a reload request is a no-op.
    reg.reload("atlas");
    EXPECT_EQ(b.reloads, 0);

    reg.shutdown();
    EXPECT_EQ(b.destroyed, (std::vector<uint32_t>{100, 101}));
}

TEST(AssetRegistry, ShutdownDestroysEverythingItOwns) {
    Backing b;
    AssetRegistry<FakeHandle> reg;
    reg.init(b.ops());

    const auto a = reg.load("a.png");
    const auto c = reg.load("c.png");
    reg.shutdown();

    ASSERT_EQ(b.destroyed.size(), 2u);
    EXPECT_EQ(reg.size(), 0u);
    // Both handles were released, in whichever order the table yielded them.
    EXPECT_TRUE(b.destroyed[0] == a.id || b.destroyed[1] == a.id);
    EXPECT_TRUE(b.destroyed[0] == c.id || b.destroyed[1] == c.id);
}

TEST(AssetRegistry, InPlaceReloadKeepsTheHandleAndStaysQuiet) {
    Backing b;
    b.reload_changes = false;
    AssetRegistry<FakeHandle> reg;
    reg.init(b.ops());

    int notifications = 0;
    reg.add_listener([&](std::string_view, FakeHandle) { ++notifications; });

    const auto original = reg.load("tex.png");
    reg.reload("tex.png");

    EXPECT_EQ(b.reloads, 1);
    EXPECT_EQ(notifications, 0);
    EXPECT_EQ(reg.get("tex.png").id, original.id);
}

TEST(AssetRegistry, RecreatingReloadRebindsTheKeyAndNotifies) {
    Backing b;
    AssetRegistry<FakeHandle> reg;
    reg.init(b.ops());

    std::vector<std::string> notified;
    FakeHandle latest{};
    reg.add_listener([&](std::string_view key, FakeHandle h) {
        notified.emplace_back(key);
        latest = h;
    });

    const auto original = reg.load("tex.png");
    reg.reload("tex.png");

    EXPECT_EQ(b.reloads, 1);
    EXPECT_EQ(notified, std::vector<std::string>{"tex.png"});
    EXPECT_NE(latest.id, original.id);
    EXPECT_EQ(reg.get("tex.png").id, latest.id);
}

TEST(AssetRegistry, ReloadingAnUnknownKeyDoesNothing) {
    Backing b;
    AssetRegistry<FakeHandle> reg;
    reg.init(b.ops());

    int notifications = 0;
    reg.add_listener([&](std::string_view, FakeHandle) { ++notifications; });

    reg.load("tex.png");
    reg.reload("other.png");

    EXPECT_EQ(b.reloads, 0);
    EXPECT_EQ(notifications, 0);
}

namespace {

/// The smallest image stb_image will decode without a compressor behind it: an
/// uncompressed 32-bit TGA. 18-byte header, then w*h BGRA pixels. Written
/// binary, so the 0x0A bytes in it survive on Windows.
void write_tga(const std::filesystem::path& path, uint16_t w, uint16_t h) {
    // offset, colour-map type, image type 2 (uncompressed true colour), then
    // the colour-map spec and x/y origin, all zero.
    std::vector<uint8_t> bytes = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bytes.push_back(static_cast<uint8_t>(w & 0xFF));
    bytes.push_back(static_cast<uint8_t>(w >> 8));
    bytes.push_back(static_cast<uint8_t>(h & 0xFF));
    bytes.push_back(static_cast<uint8_t>(h >> 8));
    bytes.push_back(32);   // bits per pixel
    bytes.push_back(0x28); // top-left origin, 8 alpha bits
    bytes.resize(bytes.size() + static_cast<size_t>(w) * h * 4, 0xFF);

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

/// One scratch directory per test, because ctest may run them concurrently in
/// separate processes and a shared name would have them fighting over one file.
class AssetCacheTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir =
            std::filesystem::temp_directory_path() / ("vaxelis_ac_" + std::string(info->name()));
        std::filesystem::remove_all(m_dir);
        std::filesystem::create_directories(m_dir);
        m_file = m_dir / "pixels.tga";
    }
    void TearDown() override { std::filesystem::remove_all(m_dir); }

    std::string path() const { return m_file.string(); }

    std::filesystem::path m_dir;
    std::filesystem::path m_file;
};

} // namespace

TEST_F(AssetCacheTest, TheRequestedFilterReachesTheDevice) {
    write_tga(m_file, 4, 4);
    RecordingDevice dev;
    AssetCache cache;
    ASSERT_TRUE(cache.init(dev));

    ASSERT_TRUE(cache.load_texture(path(), "art", rhi::TextureFilter::Nearest).valid());
    ASSERT_EQ(dev.textures.size(), 1u);
    EXPECT_EQ(dev.textures[0].width, 4u);
    EXPECT_EQ(dev.textures[0].height, 4u);
    EXPECT_EQ(dev.textures[0].filter, rhi::TextureFilter::Nearest);

    cache.shutdown();
}

TEST_F(AssetCacheTest, TheDefaultFilterIsLinear) {
    write_tga(m_file, 2, 2);
    RecordingDevice dev;
    AssetCache cache;
    ASSERT_TRUE(cache.init(dev));

    ASSERT_TRUE(cache.load_texture(path()).valid());
    ASSERT_EQ(dev.textures.size(), 1u);
    EXPECT_EQ(dev.textures[0].filter, rhi::TextureFilter::Linear);

    cache.shutdown();
}

TEST_F(AssetCacheTest, ARecreatingReloadAsksForTheSameFilterAgain) {
    write_tga(m_file, 4, 4);
    RecordingDevice dev;
    AssetCache cache;
    ASSERT_TRUE(cache.init(dev));
    ASSERT_TRUE(cache.load_texture(path(), "art", rhi::TextureFilter::Nearest).valid());

    // A size change is what forces the destroy-and-recreate path. Before the
    // filter was remembered, the replacement silently came back Linear.
    write_tga(m_file, 8, 8);
    cache.reload_texture("art");

    ASSERT_EQ(dev.textures.size(), 2u);
    EXPECT_EQ(dev.textures[1].width, 8u);
    EXPECT_EQ(dev.textures[1].filter, rhi::TextureFilter::Nearest);

    cache.shutdown();
}

TEST_F(AssetCacheTest, ASameSizeReloadUploadsInPlaceAndCreatesNothing) {
    write_tga(m_file, 4, 4);
    RecordingDevice dev;
    AssetCache cache;
    ASSERT_TRUE(cache.init(dev));
    const auto original = cache.load_texture(path(), "art", rhi::TextureFilter::Nearest);

    write_tga(m_file, 4, 4);
    cache.reload_texture("art");

    // Nothing was recreated, so there was no filter to get wrong, and the
    // handle callers hold is still the live one.
    EXPECT_EQ(dev.textures.size(), 1u);
    EXPECT_EQ(dev.texture_updates, 1);
    EXPECT_EQ(cache.get_texture("art").id, original.id);

    cache.shutdown();
}

TEST_F(AssetCacheTest, ASecondLoadOfABoundKeyKeepsTheFilterItAlreadyHas) {
    write_tga(m_file, 4, 4);
    RecordingDevice dev;
    AssetCache cache;
    ASSERT_TRUE(cache.init(dev));
    const auto first = cache.load_texture(path(), "art", rhi::TextureFilter::Nearest);

    // The key is already bound, so this returns what is cached rather than
    // reloading it under a different filter.
    const auto again = cache.load_texture(path(), "art", rhi::TextureFilter::Linear);
    EXPECT_EQ(again.id, first.id);
    EXPECT_EQ(dev.textures.size(), 1u);
    EXPECT_EQ(dev.textures[0].filter, rhi::TextureFilter::Nearest);

    cache.shutdown();
}

TEST_F(AssetCacheTest, TwoKeysOverOneFileCanBeSampledDifferently) {
    write_tga(m_file, 4, 4);
    RecordingDevice dev;
    AssetCache cache;
    ASSERT_TRUE(cache.init(dev));

    const auto crisp = cache.load_texture(path(), "crisp", rhi::TextureFilter::Nearest);
    const auto smooth = cache.load_texture(path(), "smooth", rhi::TextureFilter::Linear);

    EXPECT_NE(crisp.id, smooth.id);
    ASSERT_EQ(dev.textures.size(), 2u);
    EXPECT_EQ(dev.textures[0].filter, rhi::TextureFilter::Nearest);
    EXPECT_EQ(dev.textures[1].filter, rhi::TextureFilter::Linear);

    cache.shutdown();
}

TEST_F(AssetCacheTest, AMissingFileLoadsNothingAndBindsNoKey) {
    RecordingDevice dev;
    AssetCache cache;
    ASSERT_TRUE(cache.init(dev));

    EXPECT_FALSE(cache.load_texture(path(), "art", rhi::TextureFilter::Nearest).valid());
    EXPECT_TRUE(dev.textures.empty());
    EXPECT_FALSE(cache.get_texture("art").valid());

    cache.shutdown();
}
