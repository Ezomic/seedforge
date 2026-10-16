#include <worldforge/subseed.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <set>
#include <string>

using namespace worldforge;

namespace {
constexpr std::uint64_t kWorld = 0x123456789abcdef0ull;
constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
}

TEST_CASE("splitmix64 matches the published reference outputs", "[worldforge][subseed]")
{
    // Reference generator seeded with 0: state advances by the golden gamma, each output is splitmix64(state).
    constexpr std::uint64_t kGamma = 0x9e3779b97f4a7c15ull;
    STATIC_REQUIRE(splitmix64(0) == 0xe220a8397b1dcdafull);
    STATIC_REQUIRE(splitmix64(kGamma) == 0x6e789e6aa1b965f4ull);
    STATIC_REQUIRE(splitmix64(2 * kGamma) == 0x06c45d188009454full);
}

TEST_CASE("splitmix64 is a bijection on sampled inputs and copes with extremes", "[worldforge][subseed]")
{
    std::set<std::uint64_t> seen;
    for (std::uint64_t i = 0; i < 4096; ++i) {
        seen.insert(splitmix64(i));
    }
    REQUIRE(seen.size() == 4096);
    REQUIRE(splitmix64(kMax) != splitmix64(0));
    REQUIRE(splitmix64(kMax) == splitmix64(kMax));
}

TEST_CASE("subseed known-answer vectors, identical on every platform", "[worldforge][subseed]")
{
    constexpr std::int32_t kLo = std::numeric_limits<std::int32_t>::min();
    constexpr std::int32_t kHi = std::numeric_limits<std::int32_t>::max();

    STATIC_REQUIRE(regionSeed(0, 0, 0) == 0xd20d64a3dcd5b1a0ull);
    STATIC_REQUIRE(regionSeed(1, 0, 0) == 0x069c35adbb6164beull);
    STATIC_REQUIRE(regionSeed(0, 1, 0) == 0x90b79cb633164ca1ull);
    STATIC_REQUIRE(regionSeed(0, 0, 1) == 0x6ebb1bf31a1ca1b9ull);
    STATIC_REQUIRE(regionSeed(kWorld, -3, 7) == 0x591d655748535150ull);
    STATIC_REQUIRE(regionSeed(kMax, kLo, kHi) == 0xc5611384856f1567ull);

    STATIC_REQUIRE(dungeonSeed(0, "") == 0x1eea40e682face17ull);
    STATIC_REQUIRE(dungeonSeed(1, "poi") == 0x60b815c405e4d837ull);
    STATIC_REQUIRE(dungeonSeed(kWorld, "dungeon.crypt.01") == 0x13055738e103a47aull);

    constexpr std::uint64_t dungeon = 0x13055738e103a47aull;
    STATIC_REQUIRE(lootSeed(dungeon, "") == 0x83584092fef73f2bull);
    STATIC_REQUIRE(lootSeed(dungeon, "chest.a") == 0x7f78d22c61bd8993ull);
    STATIC_REQUIRE(lootSeed(dungeon, "chest.b") == 0x9097e46834e91ad0ull);

    STATIC_REQUIRE(rerollSeed(0, 0) == 0xf806b5848052ef06ull);
    STATIC_REQUIRE(rerollSeed(kWorld, 1) == 0x52445efb9a870755ull);
    STATIC_REQUIRE(rerollSeed(kMax, std::numeric_limits<std::uint32_t>::max()) == 0xcf4a9e653b4e2ba6ull);
}

TEST_CASE("changing one region id changes only that region's subseed", "[worldforge][subseed]")
{
    constexpr int kSize = 16;
    std::uint64_t before[kSize][kSize];
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            before[y][x] = regionSeed(kWorld, x, y);
        }
    }
    std::set<std::uint64_t> unique;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            unique.insert(before[y][x]);
            // Deriving after any other derivation gives the same answer: no hidden state.
            REQUIRE(regionSeed(kWorld, x, y) == before[y][x]);
        }
    }
    REQUIRE(unique.size() == static_cast<std::size_t>(kSize * kSize));

    REQUIRE(regionSeed(kWorld, 1, 2) != regionSeed(kWorld, 2, 1));
    REQUIRE(regionSeed(kWorld, -1, 0) != regionSeed(kWorld, 1, 0));
    REQUIRE(regionSeed(kWorld, 0, 0) != regionSeed(kWorld + 1, 0, 0));
}

TEST_CASE("each kind of subseed is a pure function of its inputs, so order never matters", "[worldforge][subseed]")
{
    const std::uint64_t a1 = dungeonSeed(kWorld, "poi.a");
    const std::uint64_t b1 = dungeonSeed(kWorld, "poi.b");
    const std::uint64_t b2 = dungeonSeed(kWorld, "poi.b");
    const std::uint64_t a2 = dungeonSeed(kWorld, "poi.a");
    REQUIRE(a1 == a2);
    REQUIRE(b1 == b2);
    REQUIRE(a1 != b1);

    const std::uint64_t r1 = regionSeed(kWorld, 4, 5);
    const std::uint64_t l1 = lootSeed(a1, "chest");
    REQUIRE(lootSeed(a1, "chest") == l1);
    REQUIRE(regionSeed(kWorld, 4, 5) == r1);
}

TEST_CASE("a dungeon's loot depends on that dungeon, and chests do not collide", "[worldforge][subseed]")
{
    const std::uint64_t d1 = dungeonSeed(kWorld, "poi.a");
    const std::uint64_t d2 = dungeonSeed(kWorld, "poi.b");
    REQUIRE(lootSeed(d1, "chest") != lootSeed(d2, "chest"));
    REQUIRE(lootSeed(d1, "chest.1") != lootSeed(d1, "chest.2"));
    REQUIRE(lootSeed(d1, "chest.1") != d1);
}

TEST_CASE("the kinds of subseed are domain separated", "[worldforge][subseed]")
{
    REQUIRE(regionSeed(kWorld, 0, 0) != rerollSeed(kWorld, 0));
    REQUIRE(dungeonSeed(kWorld, "") != lootSeed(kWorld, ""));
    REQUIRE(dungeonSeed(kWorld, "x") != lootSeed(kWorld, "x"));
}

TEST_CASE("subseeds are well mixed rather than clustered", "[worldforge][subseed]")
{
    // Neighbouring cells must not share high or low bits the way a counter would.
    constexpr int kCells = 1024;
    int bitCounts[64] = {};
    for (int i = 0; i < kCells; ++i) {
        const std::uint64_t s = regionSeed(kWorld, i, 0);
        for (int b = 0; b < 64; ++b) {
            bitCounts[b] += static_cast<int>((s >> b) & 1u);
        }
    }
    for (int b = 0; b < 64; ++b) {
        // Each bit is set about half the time; a counter-like bit would sit near 0 or kCells.
        REQUIRE(bitCounts[b] > kCells * 40 / 100);
        REQUIRE(bitCounts[b] < kCells * 60 / 100);
    }
}

TEST_CASE("hostile ids derive without trouble", "[worldforge][subseed]")
{
    const std::string huge(1 << 20, 'z');
    const std::string withNul("a\0b", 3);
    REQUIRE(dungeonSeed(kWorld, huge) == dungeonSeed(kWorld, huge));
    REQUIRE(dungeonSeed(kWorld, withNul) != dungeonSeed(kWorld, "a"));
    REQUIRE(dungeonSeed(kWorld, "") != dungeonSeed(kWorld, std::string(1, '\0')));
    REQUIRE(dungeonSeed(kMax, "\xff\xfe") == dungeonSeed(kMax, "\xff\xfe"));
    REQUIRE(regionSeed(0, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::min()) !=
            regionSeed(0, std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max()));
}
