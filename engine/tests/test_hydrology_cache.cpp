#include "ichor/world/forge.h"
#include "ichor/world/hydrology_cache.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace ichor;

namespace {

WorldDesc smallWorld(std::uint64_t seed)
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

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& name)
    {
#ifdef _WIN32
        const auto pid = static_cast<unsigned long>(_getpid());
#else
        const auto pid = static_cast<unsigned long>(getpid());
#endif
        path = std::filesystem::temp_directory_path() / ("ichor-hydrology-cache-" + name + "-" + std::to_string(pid));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir()
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

}

TEST_CASE("a cached hydrology is bit-identical to computing it, on a forged world")
{
    const Heightfield field = forgeHeightfield(smallWorld(7));
    const Hydrology computed = computeHydrology(field);
    REQUIRE_FALSE(computed.empty());

    Hydrology decoded;
    REQUIRE(decodeHydrologyCache(encodeHydrologyCache(field, computed), field, decoded));
    REQUIRE(identical(decoded, computed));
}

TEST_CASE("the first run writes the cache and later runs load it")
{
    TempDir dir("roundtrip");
    const Heightfield field = forgeHeightfield(smallWorld(7));
    const Hydrology computed = computeHydrology(field);

    REQUIRE(identical(loadOrComputeHydrology(dir.path, field), computed));
    REQUIRE(std::filesystem::exists(dir.path / "hydrology.ihyd"));
    const auto written = slurp(dir.path / "hydrology.ihyd");

    REQUIRE(identical(loadOrComputeHydrology(dir.path, field), computed));
    REQUIRE(slurp(dir.path / "hydrology.ihyd") == written);

    for (const auto& entry : std::filesystem::directory_iterator(dir.path)) {
        REQUIRE(entry.path().filename() == "hydrology.ihyd");
    }
}

TEST_CASE("a cache that is empty, cut short or has any bit flipped is recomputed and rewritten")
{
    TempDir dir("damage");
    const Heightfield field = forgeHeightfield(smallWorld(3));
    const Hydrology computed = computeHydrology(field);
    const auto good = encodeHydrologyCache(field, computed);
    const auto path = dir.path / "hydrology.ihyd";

    Hydrology out;
    SECTION("truncated at every length")
    {
        for (std::size_t length = 0; length < good.size(); ++length) {
            const std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
            REQUIRE_FALSE(decodeHydrologyCache(cut, field, out));
            REQUIRE(out.empty());
        }
    }
    SECTION("one bit flipped anywhere")
    {
        for (std::size_t at = 0; at < good.size(); at += 1 + at / 64) {
            auto bad = good;
            bad[at] ^= static_cast<std::uint8_t>(1u << (at % 8));
            REQUIRE_FALSE(decodeHydrologyCache(bad, field, out));
        }
        auto last = good;
        last.back() ^= 0x80;
        REQUIRE_FALSE(decodeHydrologyCache(last, field, out));
    }
    SECTION("an empty file on disk")
    {
        spit(path, {});
        REQUIRE(identical(loadOrComputeHydrology(dir.path, field), computed));
        REQUIRE(slurp(path) == good);
    }
    SECTION("a flipped bit on disk")
    {
        auto bad = good;
        bad[good.size() / 2] ^= 1;
        spit(path, bad);
        REQUIRE(identical(loadOrComputeHydrology(dir.path, field), computed));
        REQUIRE(slurp(path) == good);
    }
    SECTION("trailing bytes")
    {
        auto bad = good;
        bad.push_back(0);
        REQUIRE_FALSE(decodeHydrologyCache(bad, field, out));
    }
}

TEST_CASE("a cache of another version, seed or set of heights is not used")
{
    TempDir dir("foreign");
    const Heightfield field = forgeHeightfield(smallWorld(3));
    const Heightfield other = forgeHeightfield(smallWorld(4));
    const auto path = dir.path / "hydrology.ihyd";
    Hydrology out;

    SECTION("another seed")
    {
        const auto theirs = encodeHydrologyCache(other, computeHydrology(other));
        REQUIRE_FALSE(decodeHydrologyCache(theirs, field, out));
        spit(path, theirs);
        REQUIRE(identical(loadOrComputeHydrology(dir.path, field), computeHydrology(field)));
        REQUIRE(decodeHydrologyCache(slurp(path), field, out));
    }
    SECTION("the same description with different heights")
    {
        auto samples = field.samples();
        samples[samples.size() / 2] ^= 0x40;
        const Heightfield edited(field.desc(), samples);
        const auto stale = encodeHydrologyCache(field, computeHydrology(field));
        REQUIRE_FALSE(decodeHydrologyCache(stale, edited, out));
    }
    SECTION("another size")
    {
        WorldDesc bigger = smallWorld(3);
        bigger.sizeMetres = 1024;
        const Heightfield large = forgeHeightfield(bigger);
        REQUIRE_FALSE(decodeHydrologyCache(encodeHydrologyCache(large, computeHydrology(large)), field, out));
    }
    SECTION("another version, even with a valid checksum")
    {
        auto bytes = encodeHydrologyCache(field, computeHydrology(field));
        const std::uint32_t version = kHydrologyCacheVersion + 1;
        std::memcpy(bytes.data() + 4, &version, sizeof(version));
        // Sealed the way the encoder seals it, so that only the version is wrong.
        std::uint64_t h = 14695981039346656037ull;
        for (std::size_t i = 0; i + 8 < bytes.size(); ++i) {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
        std::memcpy(bytes.data() + bytes.size() - 8, &h, sizeof(h));
        REQUIRE_FALSE(decodeHydrologyCache(bytes, field, out));
    }
}

TEST_CASE("a world with no samples has no cache to write or read")
{
    TempDir dir("empty");
    const Heightfield none;
    REQUIRE(loadOrComputeHydrology(dir.path, none).empty());
    REQUIRE_FALSE(std::filesystem::exists(dir.path / "hydrology.ihyd"));
    Hydrology out;
    REQUIRE_FALSE(decodeHydrologyCache(encodeHydrologyCache(none, Hydrology{}), none, out));
}

TEST_CASE("a directory that cannot be written to still gives the hydrology")
{
    TempDir dir("unwritable");
    const Heightfield field = forgeHeightfield(smallWorld(5));
    const Hydrology computed = computeHydrology(field);

    SECTION("the directory does not exist")
    {
        REQUIRE(identical(loadOrComputeHydrology(dir.path / "missing" / "deeper", field), computed));
    }
    SECTION("the cache path is a directory")
    {
        std::filesystem::create_directories(dir.path / "hydrology.ihyd");
        REQUIRE(identical(loadOrComputeHydrology(dir.path, field), computed));
        REQUIRE(std::filesystem::is_directory(dir.path / "hydrology.ihyd"));
    }
}

TEST_CASE("concurrent writers leave one intact cache and no temporary files")
{
    TempDir dir("concurrent");
    const Heightfield field = forgeHeightfield(smallWorld(9));
    const Hydrology computed = computeHydrology(field);
    std::vector<Hydrology> results(4);
    {
        std::vector<std::jthread> threads;
        for (auto& result : results) {
            threads.emplace_back([&] { result = loadOrComputeHydrology(dir.path, field); });
        }
    }
    for (const Hydrology& result : results) {
        REQUIRE(identical(result, computed));
    }
    Hydrology out;
    REQUIRE(decodeHydrologyCache(slurp(dir.path / "hydrology.ihyd"), field, out));
    for (const auto& entry : std::filesystem::directory_iterator(dir.path)) {
        REQUIRE(entry.path().filename() == "hydrology.ihyd");
    }
}
