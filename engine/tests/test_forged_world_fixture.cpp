#include "forged_world_fixture.h"
#include "test_temp.h"

#include "ichor/world/forged_game.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace ichor;
using namespace ichor::test;

namespace {

constexpr int kGrid = 64;

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

struct Scratch {
    std::filesystem::path path = test::tempPath("ichor_forged_fixture");
    Scratch()
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~Scratch()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::vector<std::uint8_t> slurp(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void spit(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::filesystem::path> entryFiles(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(directory)) {
        files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool sameResult(const ForgeJobResult& a, const ForgeJobResult& b)
{
    return a.world && b.world && a.world->worldHash() == b.world->worldHash() && a.heightfield.digest() == b.heightfield.digest()
        && a.heightfield.samples() == b.heightfield.samples() && worldforge::encodeManifest(*a.world->manifest()) == worldforge::encodeManifest(*b.world->manifest());
}

}

TEST_CASE("a cached world is the world a fresh forge makes", "[forged_world_fixture]")
{
    const Scratch scratch;
    const ForgedFixture fresh = forgeFixture(descFor(3), kGrid);
    REQUIRE(fresh.ok());

    const ForgedFixture first = loadForgedFixture(scratch.path, descFor(3), kGrid);
    REQUIRE(first.ok());
    CHECK_FALSE(first.fromCache);
    const ForgedFixture second = loadForgedFixture(scratch.path, descFor(3), kGrid);
    REQUIRE(second.ok());
    CHECK(second.fromCache);

    CHECK(sameResult(first.result, fresh.result));
    CHECK(sameResult(second.result, fresh.result));
    CHECK(second.result.world->phaseHashes() == fresh.result.world->phaseHashes());

    const auto gameFresh = buildForgedGame(fresh.result, descFor(3));
    const auto gameCached = buildForgedGame(second.result, descFor(3));
    REQUIRE(gameFresh->ok());
    REQUIRE(gameCached->ok());
    CHECK(gameCached->stateHash() == gameFresh->stateHash());
}

TEST_CASE("another seed, grid or variant is another entry", "[forged_world_fixture]")
{
    const Scratch scratch;
    REQUIRE(loadForgedFixture(scratch.path, descFor(3), kGrid).ok());
    const std::size_t one = entryFiles(scratch.path).size();

    const ForgedFixture otherSeed = loadForgedFixture(scratch.path, descFor(4), kGrid);
    const ForgedFixture otherGrid = loadForgedFixture(scratch.path, descFor(3), 48);
    ForgedVariant river;
    river.riverThreshold = 48;
    const ForgedFixture otherConfig = loadForgedFixture(scratch.path, descFor(3), kGrid, river);
    for (const ForgedFixture* f : {&otherSeed, &otherGrid, &otherConfig}) {
        REQUIRE(f->ok());
        CHECK_FALSE(f->fromCache);
    }
    CHECK(entryFiles(scratch.path).size() == 4 * one);
    CHECK(otherSeed.result.world->worldHash() != forgeFixture(descFor(3), kGrid).result.world->worldHash());

    ForgedVariant lists;
    lists.dungeonArchetypes = {"crypt"};
    CHECK(ForgedVariant{}.hash() == kDefaultForgeConfigHash);
    CHECK(lists.hash() != kDefaultForgeConfigHash);
    CHECK(lists.hash() != river.hash());
    ForgedVariant moved;
    moved.itemIds = {"crypt"};
    CHECK(moved.hash() != lists.hash());
}

TEST_CASE("a missing, truncated or damaged entry is forged again and repaired", "[forged_world_fixture][hostile]")
{
    const ForgedFixture fresh = forgeFixture(descFor(5), kGrid);
    REQUIRE(fresh.ok());

    for (const std::string extension : {".ifwc", ".ifwp"}) {
        enum class Damage { Delete, Empty, Truncate, FlipHeader, FlipMiddle, FlipSum, Garbage };
        for (const Damage damage : {Damage::Delete, Damage::Empty, Damage::Truncate, Damage::FlipHeader, Damage::FlipMiddle, Damage::FlipSum, Damage::Garbage}) {
            INFO(extension << " damage " << static_cast<int>(damage));
            const Scratch scratch;
            REQUIRE(loadForgedFixture(scratch.path, descFor(5), kGrid).ok());
            std::filesystem::path target;
            for (const auto& file : entryFiles(scratch.path)) {
                if (file.extension() == extension) {
                    target = file;
                }
            }
            REQUIRE_FALSE(target.empty());
            std::vector<std::uint8_t> bytes = slurp(target);
            REQUIRE(bytes.size() > 64);
            switch (damage) {
            case Damage::Delete: std::filesystem::remove(target); break;
            case Damage::Empty: spit(target, {}); break;
            case Damage::Truncate: bytes.resize(bytes.size() / 2); spit(target, bytes); break;
            case Damage::FlipHeader: bytes[9] ^= 0x40; spit(target, bytes); break;
            case Damage::FlipMiddle: bytes[bytes.size() / 2] ^= 0x01; spit(target, bytes); break;
            case Damage::FlipSum: bytes.back() ^= 0xff; spit(target, bytes); break;
            case Damage::Garbage: spit(target, std::vector<std::uint8_t>(5000, 0xAB)); break;
            }

            const ForgedFixture repaired = loadForgedFixture(scratch.path, descFor(5), kGrid);
            REQUIRE(repaired.ok());
            CHECK_FALSE(repaired.fromCache);
            CHECK(sameResult(repaired.result, fresh.result));
            const ForgedFixture again = loadForgedFixture(scratch.path, descFor(5), kGrid);
            REQUIRE(again.ok());
            CHECK(again.fromCache);
            CHECK(sameResult(again.result, fresh.result));
        }
    }
}

TEST_CASE("an entry of another generator digest is never read", "[forged_world_fixture]")
{
    const Scratch scratch;
    const std::string digest = kForgedCacheDigest;
    REQUIRE(digest.size() == 16);
    CHECK(std::all_of(digest.begin(), digest.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }));

    REQUIRE(loadForgedFixture(scratch.path / "aaaaaaaaaaaaaaaa", descFor(6), kGrid).ok());
    const ForgedFixture other = loadForgedFixture(scratch.path / "bbbbbbbbbbbbbbbb", descFor(6), kGrid);
    REQUIRE(other.ok());
    CHECK_FALSE(other.fromCache);
}

TEST_CASE("processes that start together share one entry and never read a half written file", "[forged_world_fixture]")
{
    const Scratch scratch;
    const ForgedFixture fresh = forgeFixture(descFor(7), kGrid);
    REQUIRE(fresh.ok());

    constexpr int kThreads = 6;
    std::atomic<int> good{0};
    std::atomic<int> hits{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            const ForgedFixture f = loadForgedFixture(scratch.path, descFor(7), kGrid);
            if (f.ok() && f.result.world->worldHash() == fresh.result.world->worldHash() && f.result.heightfield.samples() == fresh.result.heightfield.samples()) {
                ++good;
            }
            if (f.fromCache) {
                ++hits;
            }
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    CHECK(good == kThreads);
    CHECK(hits >= 1);
    for (const auto& file : entryFiles(scratch.path)) {
        const std::string ext = file.extension().string();
        CHECK((ext == ".ifwc" || ext == ".ifwp"));
    }
}

TEST_CASE("a world the forge refuses is an error and writes nothing", "[forged_world_fixture][hostile]")
{
    const Scratch scratch;
    const ForgedFixture refused = loadForgedFixture(scratch.path, descFor(8), 4);
    CHECK_FALSE(refused.ok());
    CHECK_FALSE(refused.error.empty());
    CHECK(entryFiles(scratch.path).empty());
}

TEST_CASE("an unwritable cache folder only costs the forge", "[forged_world_fixture][hostile]")
{
    const Scratch scratch;
    const std::filesystem::path blocker = scratch.path / "file";
    spit(blocker, {1, 2, 3});
    const ForgedFixture f = loadForgedFixture(blocker / "cache", descFor(9), kGrid);
    REQUIRE(f.ok());
    CHECK_FALSE(f.fromCache);
}
