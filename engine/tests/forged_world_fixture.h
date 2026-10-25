#pragma once

#include "forged_cache_digest.h"

#include "ichor/core/file_io.h"
#include "ichor/core/hash.h"
#include "ichor/world/forge_job.h"
#include "ichor/world/forged_phase_maps.h"
#include "ichor/world/forged_world_cache.h"

#include <catch2/catch_test_macros.hpp>

#include <worldforge/forge_world.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ichor::test {

// A forged world shared by every test process through the build directory (SEED-665). ctest runs each Catch2 case as its own
// process, so without this dozens of cases forge the same seed and grid again. The first process to need a world forges it and
// writes it; the rest read it back.
//
// An entry is the forged world file (forged_world_cache.h) and the phase maps beside it (forged_phase_maps.h), so what comes
// back is what buildForgedGame reads: the six phase maps, the roads' terrain and the manifest. Tectonics, erosion, climate and
// resources are not stored, so a test that reads those forges with worldforge::forgeWorld itself.
//
// The folder holds one subfolder per generator digest (forged_cache_digest.h, a hash of libworldforge and the engine's encoding
// code), so a change to the generator or the format is a miss, never a stale hit. An entry is written to a temporary name and
// renamed into place, so a reader never sees half a file. Every read is validated with the formats' own checksums and stamps; a
// missing, truncated or damaged entry costs a forge and nothing else. Set ICHOR_FORGED_CACHE_DIR to move the cache, or to "off"
// to forge every time.

// What differs between the forges the tests ask for. The config is built from these fields alone, so the cache key covers every
// way the forge can differ; a test that needs another ForgeConfig field forges directly.
struct ForgedVariant {
    std::vector<std::string> itemIds;
    std::vector<std::string> npcArchetypes;
    std::vector<std::string> dungeonArchetypes;
    std::optional<int> riverThreshold;

    worldforge::ForgeConfig config() const
    {
        worldforge::ForgeConfig out;
        out.itemIds = itemIds;
        out.npcArchetypes = npcArchetypes;
        out.dungeonArchetypes = dungeonArchetypes;
        if (riverThreshold) {
            out.hydrology.riverThreshold = *riverThreshold;
        }
        return out;
    }

    std::uint64_t hash() const
    {
        if (itemIds.empty() && npcArchetypes.empty() && dungeonArchetypes.empty() && !riverThreshold) {
            return kDefaultForgeConfigHash;
        }
        std::string text;
        const auto append = [&text](std::string_view name, const std::vector<std::string>& list) {
            text += std::string(name) + std::to_string(list.size()) + "[";
            for (const std::string& s : list) {
                text += std::to_string(s.size()) + ":" + s + ",";
            }
            text += "]";
        };
        append("items", itemIds);
        append("npcs", npcArchetypes);
        append("dungeons", dungeonArchetypes);
        text += riverThreshold ? "river=" + std::to_string(*riverThreshold) : std::string("river=default");
        const std::uint64_t h = hash64(text);
        return h == kDefaultForgeConfigHash ? 1 : h;
    }
};

// The variant a content-bearing config (loadForgedContentSetup) stands for: it names items, cast and dungeon themes and nothing else.
inline ForgedVariant variantOfContentLists(const worldforge::ForgeConfig& config)
{
    ForgedVariant variant;
    variant.itemIds = config.itemIds;
    variant.npcArchetypes = config.npcArchetypes;
    variant.dungeonArchetypes = config.dungeonArchetypes;
    return variant;
}

struct ForgedFixture {
    ForgeJobResult result; // world is null when the forge failed
    bool fromCache = false;
    std::string error;

    bool ok() const { return error.empty() && result.world != nullptr; }
};

namespace fixture_detail {

constexpr std::uintmax_t kMaxPhaseBytes = std::uintmax_t{512} << 20;
constexpr auto kLockWait = std::chrono::seconds(90);
constexpr auto kLockStale = std::chrono::minutes(10);

inline ForgedWorldKey keyFor(const WorldDesc& desc, int gridSize, const ForgedVariant& variant)
{
    ForgedWorldKey key = forgedWorldKey(desc, gridSize);
    key.configHash = variant.hash();
    return key;
}

inline std::filesystem::path phasesPath(const std::filesystem::path& directory, const ForgedWorldKey& key)
{
    std::filesystem::path path = forgedWorldCachePath(directory, key);
    path.replace_extension(".ifwp");
    return path;
}

inline std::filesystem::path lockPath(const std::filesystem::path& directory, const ForgedWorldKey& key)
{
    std::filesystem::path path = forgedWorldCachePath(directory, key);
    path.replace_extension(".lock");
    return path;
}

// Both files must be present and intact, and agree with each other; anything else is a miss.
inline std::optional<ForgeJobResult> readEntry(const std::filesystem::path& directory, const ForgedWorldKey& key)
{
    std::optional<CachedForgedWorld> stored = readForgedWorldCache(directory, key);
    if (!stored) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(phasesPath(directory, key), bytes, kMaxPhaseBytes)) {
        return std::nullopt;
    }
    std::shared_ptr<const worldforge::ForgedWorld> world = decodeForgedPhaseMaps(bytes, key, *stored);
    if (!world) {
        return std::nullopt;
    }
    ForgeJobResult result;
    result.world = std::move(world);
    result.heightfield = std::move(stored->heightfield);
    return result;
}

inline void writeEntry(const std::filesystem::path& directory, const ForgedWorldKey& key, const worldforge::ForgedWorld& forged, const ForgeJobResult& result)
{
    std::vector<std::uint8_t> phases = encodeForgedPhaseMaps(key, forged);
    if (phases.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    CachedForgedWorld stored;
    stored.worldHash = forged.worldHash();
    stored.heightfield = result.heightfield;
    stored.manifest = *forged.manifest();
    // The phase maps first: a reader that finds the world file always finds these.
    if (writeFileAtomically(phasesPath(directory, key), phases)) {
        writeForgedWorldCache(directory, key, stored);
    }
}

// Holds the right to forge one entry, so processes that start together forge a world once between them.
class EntryLock {
public:
    EntryLock(const std::filesystem::path& directory, const ForgedWorldKey& key)
        : path_(lockPath(directory, key))
    {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        const auto written = std::filesystem::last_write_time(path_, ec);
        if (!ec && std::filesystem::file_time_type::clock::now() - written > kLockStale) {
            std::filesystem::remove(path_, ec);
        }
        held_ = std::filesystem::create_directory(path_, ec) && !ec;
    }
    ~EntryLock()
    {
        if (held_) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
    }
    EntryLock(const EntryLock&) = delete;
    EntryLock& operator=(const EntryLock&) = delete;

    bool held() const { return held_; }
    bool exists() const
    {
        std::error_code ec;
        return std::filesystem::exists(path_, ec);
    }

private:
    std::filesystem::path path_;
    bool held_ = false;
};

}

// Forges the world from scratch, with no cache involved. Same result a cache miss produces.
inline ForgedFixture forgeFixture(const WorldDesc& desc, int gridSize, const ForgedVariant& variant = {})
{
    ForgedFixture out;
    auto forged = std::make_shared<const worldforge::ForgedWorld>(worldforge::forgeWorld(desc.seed, gridSize, variant.config()));
    if (!forged->complete() || !forged->manifest()) {
        out.error = forged->ok() ? "the forge stopped early" : forged->error();
        return out;
    }
    ForgedHeightfield adapted = heightfieldFromForgedWorld(*forged, forgedWorldDesc(desc));
    if (!adapted.ok()) {
        out.error = adapted.error;
        return out;
    }
    out.result.world = std::move(forged);
    out.result.heightfield = std::move(adapted.field);
    return out;
}

// The world for (desc, gridSize, variant) from the cache in `directory`, forged and stored there on a miss. Never fails
// because of the cache: a bad entry or an unwritable folder only means a forge.
inline ForgedFixture loadForgedFixture(const std::filesystem::path& directory, const WorldDesc& desc, int gridSize, const ForgedVariant& variant = {})
{
    const ForgedWorldKey key = fixture_detail::keyFor(desc, gridSize, variant);
    ForgedFixture out;
    if (std::optional<ForgeJobResult> hit = fixture_detail::readEntry(directory, key)) {
        out.result = std::move(*hit);
        out.fromCache = true;
        return out;
    }

    fixture_detail::EntryLock lock(directory, key);
    if (!lock.held()) {
        const auto giveUp = std::chrono::steady_clock::now() + fixture_detail::kLockWait;
        while (lock.exists() && std::chrono::steady_clock::now() < giveUp) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (std::optional<ForgeJobResult> hit = fixture_detail::readEntry(directory, key)) {
            out.result = std::move(*hit);
            out.fromCache = true;
            return out;
        }
    }

    out = forgeFixture(desc, gridSize, variant);
    if (out.ok()) {
        fixture_detail::writeEntry(directory, key, *out.result.world, out.result);
    }
    return out;
}

// The shared cache folder for this build and generator, or empty when caching is off. The first use after the generator changed
// removes the folders of older digests.
inline std::filesystem::path forgedFixtureDirectory()
{
    static const std::filesystem::path directory = [] {
        const char* env = std::getenv("ICHOR_FORGED_CACHE_DIR");
        const std::string base = env != nullptr && *env != '\0' ? env : ICHOR_FORGED_CACHE_DIR;
        if (base == "off") {
            return std::filesystem::path();
        }
        const std::filesystem::path current = std::filesystem::path(base) / kForgedCacheDigest;
        std::error_code ec;
        if (!std::filesystem::exists(current, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
                const std::string name = entry.path().filename().string();
                const bool isDigestFolder = name.size() == 16 && name.find_first_not_of("0123456789abcdef") == std::string::npos;
                if (isDigestFolder && entry.is_directory(ec) && entry.path() != current) {
                    std::filesystem::remove_all(entry.path(), ec);
                }
            }
            std::filesystem::create_directories(current, ec);
        }
        return current;
    }();
    return directory;
}

inline ForgedFixture forgedFixture(const WorldDesc& desc, int gridSize, const ForgedVariant& variant = {})
{
    const std::filesystem::path directory = forgedFixtureDirectory();
    return directory.empty() ? forgeFixture(desc, gridSize, variant) : loadForgedFixture(directory, desc, gridSize, variant);
}

// For the tests: the shared world, or a failed REQUIRE that says why the forge failed.
inline ForgeJobResult forgedWorldFixture(const WorldDesc& desc, int gridSize, const ForgedVariant& variant = {})
{
    ForgedFixture fixture = forgedFixture(desc, gridSize, variant);
    INFO(fixture.error);
    REQUIRE(fixture.ok());
    return std::move(fixture.result);
}

}
