#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets/AssetRegistry.hpp"

using namespace vaxelis;

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
