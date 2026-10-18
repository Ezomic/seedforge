#include "ichor/world/forged_world_cache.h"

#include "ichor/core/binary.h"
#include "ichor/core/file_io.h"
#include "ichor/core/hash.h"
#include "ichor/core/log.h"

#include <worldforge/forge_world.h>
#include <worldforge/gen_version.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace ichor {

namespace {

constexpr std::array<char, 4> kMagic{'I', 'F', 'W', 'C'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kSumBytes = sizeof(std::uint64_t);
// The largest valid height grid (WorldDesc::valid allows 8193 samples a side) plus room for the manifest.
constexpr std::uintmax_t kMaxEntryBytes = 8193ull * 8193ull * sizeof(std::uint16_t) + (16ull << 20);

std::uint64_t checksum(const std::uint8_t* data, std::size_t size)
{
    return hash64(std::string_view(reinterpret_cast<const char*>(data), size));
}

std::uint64_t descHash(const WorldDesc& desc)
{
    BinaryWriter w;
    w.write(desc);
    return checksum(w.bytes().data(), w.bytes().size());
}

}

ForgedWorldKey forgedWorldKey(const WorldDesc& desc, int gridSize)
{
    ForgedWorldKey key;
    key.seed = desc.seed;
    key.genVersion = worldforge::kGenVersion;
    key.gridSize = gridSize;
    key.configHash = kDefaultForgeConfigHash;
    key.descHash = descHash(forgedWorldDesc(desc));
    return key;
}

std::vector<std::uint8_t> encodeForgedWorldCache(const ForgedWorldKey& key, const CachedForgedWorld& world)
{
    BinaryWriter w;
    w.write(kMagic);
    w.write(kVersion);
    w.write(key.seed);
    w.write(key.genVersion);
    w.write(key.gridSize);
    w.write(key.configHash);
    w.write(key.descHash);
    w.write(world.worldHash);
    w.write(world.heightfield.desc());
    w.write(world.heightfield.digest());
    w.write(world.heightfield.samples());
    w.write(worldforge::encodeManifest(world.manifest));
    std::vector<std::uint8_t> bytes = w.take();
    const std::uint64_t sum = checksum(bytes.data(), bytes.size());
    BinaryWriter tail;
    tail.write(sum);
    bytes.insert(bytes.end(), tail.bytes().begin(), tail.bytes().end());
    return bytes;
}

bool decodeForgedWorldCache(const std::vector<std::uint8_t>& bytes, const ForgedWorldKey& key, CachedForgedWorld& out)
{
    out = CachedForgedWorld{};
    if (bytes.size() < kSumBytes) {
        return false;
    }
    const std::size_t bodySize = bytes.size() - kSumBytes;
    std::uint64_t stored = 0;
    std::memcpy(&stored, bytes.data() + bodySize, kSumBytes);
    if (stored != checksum(bytes.data(), bodySize)) {
        return false;
    }

    BinaryReader r(std::span<const std::uint8_t>(bytes.data(), bodySize));
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    ForgedWorldKey stamped;
    CachedForgedWorld world;
    WorldDesc desc;
    std::uint64_t digest = 0;
    std::vector<std::uint16_t> samples;
    std::vector<std::uint8_t> manifestBytes;
    if (!r.read(magic) || magic != kMagic || !r.read(version) || version != kVersion || !r.read(stamped.seed) || !r.read(stamped.genVersion)
        || !r.read(stamped.gridSize) || !r.read(stamped.configHash) || !r.read(stamped.descHash) || stamped != key || !r.read(world.worldHash) || !r.read(desc)
        || !desc.valid() || descHash(desc) != key.descHash || !r.read(digest) || !r.read(samples) || !r.read(manifestBytes) || !r.atEnd()) {
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(desc.samplesPerSide()) * desc.samplesPerSide();
    if (samples.size() != expected) {
        return false;
    }
    world.heightfield = Heightfield(desc, std::move(samples));
    if (world.heightfield.digest() != digest) {
        return false;
    }
    std::optional<worldforge::ContentManifest> manifest = worldforge::decodeManifest(manifestBytes);
    if (!manifest || manifest->worldSeed != key.seed || manifest->version != worldforge::kManifestVersionGen || manifest->genVersion != key.genVersion) {
        return false;
    }
    world.manifest = std::move(*manifest);
    out = std::move(world);
    return true;
}

std::filesystem::path forgedWorldCachePath(const std::filesystem::path& directory, const ForgedWorldKey& key)
{
    char name[160];
    std::snprintf(name, sizeof(name), "world_%016llx_g%u_n%d_c%016llx_d%016llx.ifwc", static_cast<unsigned long long>(key.seed), key.genVersion, key.gridSize,
        static_cast<unsigned long long>(key.configHash), static_cast<unsigned long long>(key.descHash));
    return directory / name;
}

std::optional<CachedForgedWorld> readForgedWorldCache(const std::filesystem::path& directory, const ForgedWorldKey& key)
{
    const std::filesystem::path path = forgedWorldCachePath(directory, key);
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(path, bytes, kMaxEntryBytes)) {
        return std::nullopt;
    }
    CachedForgedWorld world;
    if (!decodeForgedWorldCache(bytes, key, world)) {
        logf(LogLevel::Warn, "{} is stale or damaged; forging the world again", path.string());
        return std::nullopt;
    }
    return world;
}

bool writeForgedWorldCache(const std::filesystem::path& directory, const ForgedWorldKey& key, const CachedForgedWorld& world)
{
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    return writeFileAtomically(forgedWorldCachePath(directory, key), encodeForgedWorldCache(key, world));
}

LoadedForgedWorld loadOrForgeWorld(const std::filesystem::path& directory, const WorldDesc& desc, int gridSize)
{
    LoadedForgedWorld loaded;
    const ForgedWorldKey key = forgedWorldKey(desc, gridSize);
    if (std::optional<CachedForgedWorld> cached = readForgedWorldCache(directory, key)) {
        loaded.world = std::move(*cached);
        loaded.fromCache = true;
        return loaded;
    }

    const worldforge::ForgedWorld forged = worldforge::forgeWorld(desc.seed, gridSize);
    if (!forged.complete() || !forged.manifest()) {
        loaded.error = forged.ok() ? "the forge stopped early" : forged.error();
        return loaded;
    }
    ForgedHeightfield adapted = heightfieldFromForgedWorld(forged, forgedWorldDesc(desc));
    if (!adapted.ok()) {
        loaded.error = adapted.error;
        return loaded;
    }
    loaded.world.worldHash = forged.worldHash();
    loaded.world.heightfield = std::move(adapted.field);
    loaded.world.manifest = *forged.manifest();
    if (!writeForgedWorldCache(directory, key, loaded.world)) {
        logf(LogLevel::Warn, "cannot write the forged world cache in {}; it will be forged again next run", directory.string());
    }
    return loaded;
}

}
