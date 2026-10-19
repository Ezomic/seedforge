#include "ichor/core/hash.h"
#include "ichor/world/forged_world_cache.h"
#include "test_temp.h"

#include <worldforge/forge_world.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

using namespace ichor;

namespace {

constexpr int kGrid = 64;
// magic, version, seed, genVersion, gridSize, configHash, descHash: the world hash follows. It is informational (nothing to
// check it against without forging), so only the checksum guards it.
constexpr std::size_t kWorldHashAt = 4 + 4 + 8 + 4 + 4 + 8 + 8;

WorldDesc smallWorld(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 512;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    return desc;
}

struct Scratch {
    std::filesystem::path path = test::tempPath("ichor_forged_world_cache");
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

void reseal(std::vector<std::uint8_t>& bytes)
{
    const std::size_t body = bytes.size() - sizeof(std::uint64_t);
    const std::uint64_t sum = hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), body));
    std::memcpy(bytes.data() + body, &sum, sizeof(sum));
}

bool sameWorld(const CachedForgedWorld& a, const CachedForgedWorld& b)
{
    return a.worldHash == b.worldHash && a.heightfield.desc().seed == b.heightfield.desc().seed && a.heightfield.digest() == b.heightfield.digest()
        && a.heightfield.samples() == b.heightfield.samples() && worldforge::encodeManifest(a.manifest) == worldforge::encodeManifest(b.manifest);
}

CachedForgedWorld forgeFresh(const WorldDesc& desc)
{
    const worldforge::ForgedWorld forged = worldforge::forgeWorld(desc.seed, kGrid);
    REQUIRE(forged.complete());
    ForgedHeightfield adapted = heightfieldFromForgedWorld(forged, forgedWorldDesc(desc));
    REQUIRE(adapted.ok());
    CachedForgedWorld world;
    world.worldHash = forged.worldHash();
    world.heightfield = std::move(adapted.field);
    world.manifest = *forged.manifest();
    return world;
}

}

TEST_CASE("a cached forged world reads back equal to a fresh forge", "[forged_world_cache]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(0xF00D);

    const LoadedForgedWorld first = loadOrForgeWorld(dir.path, desc, kGrid);
    REQUIRE(first.ok());
    CHECK_FALSE(first.fromCache);
    CHECK(first.world.heightfield.samples().size() > 0);
    CHECK(first.world.manifest.genVersion == worldforge::kGenVersion);

    const LoadedForgedWorld second = loadOrForgeWorld(dir.path, desc, kGrid);
    REQUIRE(second.ok());
    CHECK(second.fromCache);
    CHECK(sameWorld(first.world, second.world));
    CHECK(sameWorld(second.world, forgeFresh(desc)));
}

TEST_CASE("the encoding is deterministic and survives decode then encode", "[forged_world_cache]")
{
    const WorldDesc desc = smallWorld(7);
    const ForgedWorldKey key = forgedWorldKey(desc, kGrid);
    const CachedForgedWorld world = forgeFresh(desc);
    const std::vector<std::uint8_t> bytes = encodeForgedWorldCache(key, world);
    CHECK(bytes == encodeForgedWorldCache(key, world));

    CachedForgedWorld decoded;
    REQUIRE(decodeForgedWorldCache(bytes, key, decoded));
    CHECK(encodeForgedWorldCache(key, decoded) == bytes);
}

TEST_CASE("a changed seed, genVersion, grid, config or desc never finds the old entry", "[forged_world_cache]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(11);
    REQUIRE(loadOrForgeWorld(dir.path, desc, kGrid).ok());
    const ForgedWorldKey key = forgedWorldKey(desc, kGrid);
    REQUIRE(readForgedWorldCache(dir.path, key).has_value());

    ForgedWorldKey otherGen = key;
    otherGen.genVersion += 1;
    ForgedWorldKey otherConfig = key;
    otherConfig.configHash = 0x1234;
    ForgedWorldKey otherGrid = key;
    otherGrid.gridSize = 128;
    ForgedWorldKey otherSeed = key;
    otherSeed.seed += 1;
    WorldDesc taller = desc;
    taller.metresPerSample = 16;
    const ForgedWorldKey otherDesc = forgedWorldKey(taller, kGrid);

    for (const ForgedWorldKey& other : {otherGen, otherConfig, otherGrid, otherSeed, otherDesc}) {
        CHECK(other != key);
        CHECK_FALSE(readForgedWorldCache(dir.path, other).has_value());
    }
}

TEST_CASE("an entry copied under another key's name is rejected by the stamp inside it", "[forged_world_cache]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(13);
    REQUIRE(loadOrForgeWorld(dir.path, desc, kGrid).ok());
    const ForgedWorldKey key = forgedWorldKey(desc, kGrid);

    ForgedWorldKey otherGen = key;
    otherGen.genVersion += 1;
    ForgedWorldKey otherConfig = key;
    otherConfig.configHash = 99;
    for (const ForgedWorldKey& other : {otherGen, otherConfig}) {
        spit(forgedWorldCachePath(dir.path, other), slurp(forgedWorldCachePath(dir.path, key)));
        CHECK_FALSE(readForgedWorldCache(dir.path, other).has_value());
    }
}

TEST_CASE("a corrupt or truncated file is forged again and replaced", "[forged_world_cache]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(21);
    const LoadedForgedWorld original = loadOrForgeWorld(dir.path, desc, kGrid);
    REQUIRE(original.ok());
    const std::filesystem::path file = forgedWorldCachePath(dir.path, forgedWorldKey(desc, kGrid));
    const std::vector<std::uint8_t> good = slurp(file);
    REQUIRE_FALSE(good.empty());

    std::vector<std::uint8_t> flipped = good;
    flipped[flipped.size() / 2] ^= 0x40;
    const std::vector<std::uint8_t> truncated(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(good.size() / 3));
    const std::vector<std::uint8_t> empty;
    const std::vector<std::uint8_t> garbage(good.size(), 0xAB);

    for (const std::vector<std::uint8_t>& bad : {flipped, truncated, empty, garbage}) {
        spit(file, bad);
        const LoadedForgedWorld again = loadOrForgeWorld(dir.path, desc, kGrid);
        REQUIRE(again.ok());
        CHECK_FALSE(again.fromCache);
        CHECK(sameWorld(again.world, original.world));
        CHECK(slurp(file) == good);
    }
    CHECK(loadOrForgeWorld(dir.path, desc, kGrid).fromCache);
}

TEST_CASE("hostile bytes are rejected without crashing", "[forged_world_cache]")
{
    const WorldDesc desc = smallWorld(19);
    const ForgedWorldKey key = forgedWorldKey(desc, kGrid);
    const std::vector<std::uint8_t> good = encodeForgedWorldCache(key, forgeFresh(desc));
    CachedForgedWorld out;
    REQUIRE(decodeForgedWorldCache(good, key, out));

    SECTION("every prefix")
    {
        const std::size_t step = good.size() / 400 + 1;
        for (std::size_t n = 0; n < good.size(); n += step) {
            CHECK_FALSE(decodeForgedWorldCache(std::vector<std::uint8_t>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n)), key, out));
            CHECK(out.heightfield.empty());
        }
        CHECK_FALSE(decodeForgedWorldCache(std::vector<std::uint8_t>(good.begin(), good.end() - 1), key, out));
    }

    SECTION("a flipped byte anywhere")
    {
        const std::size_t step = good.size() / 400 + 1;
        for (std::size_t i = 0; i < good.size(); i += step) {
            std::vector<std::uint8_t> bad = good;
            bad[i] ^= 0x01;
            CHECK_FALSE(decodeForgedWorldCache(bad, key, out));
        }
    }

    SECTION("a flipped byte with the checksum made valid again")
    {
        const std::size_t step = good.size() / 400 + 1;
        std::size_t accepted = 0;
        for (std::size_t i = 0; i + sizeof(std::uint64_t) < good.size(); i += step) {
            if (i >= kWorldHashAt && i < kWorldHashAt + sizeof(std::uint64_t)) {
                continue;
            }
            std::vector<std::uint8_t> bad = good;
            bad[i] ^= 0xFF;
            reseal(bad);
            if (decodeForgedWorldCache(bad, key, out)) {
                CHECK(encodeForgedWorldCache(key, out) == bad);
                ++accepted;
            }
        }
        CHECK(accepted < 3);
    }

    SECTION("a sample count that promises more than the file holds")
    {
        const std::uint32_t huge = 0xFFFFFFF0u;
        for (std::size_t at = 8; at + 4 < 160; ++at) {
            if (at + 4 > kWorldHashAt && at < kWorldHashAt + sizeof(std::uint64_t)) {
                continue;
            }
            std::vector<std::uint8_t> probe = good;
            std::memcpy(probe.data() + at, &huge, sizeof(huge));
            reseal(probe);
            CHECK_FALSE(decodeForgedWorldCache(probe, key, out));
        }
    }

    SECTION("empty, tiny and trailing bytes")
    {
        CHECK_FALSE(decodeForgedWorldCache({}, key, out));
        CHECK_FALSE(decodeForgedWorldCache(std::vector<std::uint8_t>(7, 0), key, out));
        CHECK_FALSE(decodeForgedWorldCache(std::vector<std::uint8_t>(64, 0xFF), key, out));
        std::vector<std::uint8_t> extra = good;
        extra.insert(extra.end() - 8, 0x00);
        reseal(extra);
        CHECK_FALSE(decodeForgedWorldCache(extra, key, out));
    }

    SECTION("a directory or missing file in place of the cache file")
    {
        Scratch dir;
        CHECK_FALSE(readForgedWorldCache(dir.path, key).has_value());
        std::filesystem::create_directories(forgedWorldCachePath(dir.path, key));
        CHECK_FALSE(readForgedWorldCache(dir.path, key).has_value());
    }
}
