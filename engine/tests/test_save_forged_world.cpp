#include "ichor/save/forge_cache.h"
#include "ichor/world/forge.h"
#include "ichor/world/hydrology.h"
#include "ichor/world/hydrology_cache.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace ichor;

// The forge cache tests use patterned bytes. These push a really forged world's hydrology through the same
// cache and back, so a change to either format that only breaks on real data goes red.

namespace {

constexpr std::uint32_t kGenVersion = 1;

WorldDesc worldFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 512;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    return desc;
}

template <class T>
bool sameBits(const std::vector<T>& a, const std::vector<T>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

bool identical(const Hydrology& a, const Hydrology& b)
{
    return a.side == b.side && sameBits(a.filled, b.filled) && sameBits(a.direction, b.direction) && sameBits(a.accumulation, b.accumulation)
        && sameBits(a.lake, b.lake) && sameBits(a.lakeDeepest, b.lakeDeepest);
}

struct Scratch {
    std::filesystem::path path = test::tempPath("ichor_save_forged_world");
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

ForgeCacheKey keyFor(std::uint64_t seed, const Heightfield& field)
{
    return {seed, kGenVersion, field.digest()};
}

}

TEST_CASE("a forged world's hydrology survives the forge cache across a restart, bit for bit", "[save_roundtrip]")
{
    Scratch dir;
    for (const std::uint64_t seed : {7ull, 42ull}) {
        const Heightfield field = forgeHeightfield(worldFor(seed));
        const Hydrology hydrology = computeHydrology(field);
        REQUIRE_FALSE(hydrology.empty());
        const std::vector<std::uint8_t> payload = encodeHydrologyCache(field, hydrology);
        REQUIRE(payload.size() > 1000); // a payload that is almost empty would pin nothing

        {
            ForgeCache cache(dir.path);
            cache.scan();
            REQUIRE(cache.put(keyFor(seed, field), payload) == ForgePutStatus::Stored);
        }

        ForgeCache reopened(dir.path);
        reopened.scan();
        const ForgeGetResult got = reopened.get(keyFor(seed, field));
        REQUIRE(got.status == ForgeGetStatus::Hit);
        REQUIRE(got.payload == payload);

        Hydrology decoded;
        REQUIRE(decodeHydrologyCache(got.payload, field, decoded));
        REQUIRE(identical(decoded, hydrology));
        REQUIRE(encodeHydrologyCache(field, decoded) == payload);
    }
}

TEST_CASE("two seeds never share a cache entry, and a payload is refused for another world's heights", "[save_roundtrip]")
{
    Scratch dir;
    const Heightfield a = forgeHeightfield(worldFor(7));
    const Heightfield b = forgeHeightfield(worldFor(42));
    REQUIRE(a.digest() != b.digest());
    const std::vector<std::uint8_t> payloadA = encodeHydrologyCache(a, computeHydrology(a));
    const std::vector<std::uint8_t> payloadB = encodeHydrologyCache(b, computeHydrology(b));
    REQUIRE(payloadA != payloadB);

    ForgeCache cache(dir.path);
    cache.scan();
    REQUIRE(cache.put(keyFor(7, a), payloadA) == ForgePutStatus::Stored);
    REQUIRE(cache.put(keyFor(42, b), payloadB) == ForgePutStatus::Stored);
    REQUIRE(cache.get(keyFor(7, a)).payload == payloadA);
    REQUIRE(cache.get(keyFor(42, b)).payload == payloadB);

    Hydrology wrong;
    REQUIRE_FALSE(decodeHydrologyCache(payloadA, b, wrong));
    REQUIRE(wrong.empty());
}

TEST_CASE("a flipped byte anywhere in a real cache file is dropped by the cache, never served", "[save_roundtrip]")
{
    Scratch dir;
    const Heightfield field = forgeHeightfield(worldFor(7));
    const std::vector<std::uint8_t> payload = encodeHydrologyCache(field, computeHydrology(field));
    const ForgeCacheKey key = keyFor(7, field);

    ForgeCache probe(dir.path);
    probe.scan();
    REQUIRE(probe.put(key, payload) == ForgePutStatus::Stored);
    const std::filesystem::path file = probe.pathFor(key);
    std::vector<std::uint8_t> pristine;
    {
        std::ifstream in(file, std::ios::binary);
        pristine.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    REQUIRE(pristine.size() > payload.size());

    const std::size_t step = pristine.size() / 64 + 1;
    std::size_t flipped = 0;
    for (std::size_t at = 0; at < pristine.size(); at += step) {
        std::vector<std::uint8_t> damaged = pristine;
        damaged[at] ^= 0x40;
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(damaged.data()), static_cast<std::streamsize>(damaged.size()));
        }
        ForgeCache cache(dir.path);
        cache.scan();
        const ForgeGetResult got = cache.get(key);
        REQUIRE(got.status != ForgeGetStatus::Hit);
        REQUIRE(got.payload.empty());
        ++flipped;
        REQUIRE(cache.put(key, payload) == ForgePutStatus::Stored);
    }
    REQUIRE(flipped >= 32);
}
