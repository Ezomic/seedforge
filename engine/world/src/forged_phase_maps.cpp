#include "ichor/world/forged_phase_maps.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"

#include <array>
#include <cstring>
#include <string_view>

namespace ichor {

namespace {

namespace wf = worldforge;

constexpr std::array<char, 4> kMagic{'I', 'F', 'W', 'P'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kSumBytes = sizeof(std::uint64_t);

// Indices into the phase hash array (phase number minus one).
constexpr std::size_t kHydrologyHash = 2;
constexpr std::size_t kBiomesHash = 4;
constexpr std::size_t kSettlementsHash = 6;
constexpr std::size_t kPoisHash = 9;
constexpr std::size_t kRoadsHash = 7;
constexpr std::size_t kPoliticsHash = 8;
constexpr std::size_t kContentHash = 10;

using PhaseHashes = std::array<std::uint64_t, wf::kForgePhaseCount>;

std::uint64_t checksum(const std::uint8_t* data, std::size_t size)
{
    return hash64(std::string_view(reinterpret_cast<const char*>(data), size));
}

std::size_t cellCount(int size)
{
    return static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
}

void putHydrology(BinaryWriter& w, const wf::HydrologyMap& m)
{
    w.write(m.seed);
    w.write(m.filledM);
    w.write(m.lakeDepthM);
    w.write(m.heightM);
    w.write(m.flowDir);
    w.write(m.accumulation);
    w.write(m.river);
}

void putBiomes(BinaryWriter& w, const wf::BiomeMap& m)
{
    w.write(m.seed);
    w.write(m.biome);
    w.write(m.secondary);
    w.write(m.blend);
}

void putSettlements(BinaryWriter& w, const wf::SettlementMap& m)
{
    w.write(m.seed);
    w.write(m.suitability);
    w.write(static_cast<std::uint32_t>(m.sites.size()));
    for (const wf::Settlement& s : m.sites) {
        w.write(s.x);
        w.write(s.y);
        w.write(s.score);
        w.write(s.tier);
        w.write(s.flags);
    }
}

void putRoads(BinaryWriter& w, const wf::RoadMap& m)
{
    w.write(m.seed);
    w.write(static_cast<std::int32_t>(m.components));
    w.write(m.heightM);
    w.write(m.road);
    w.write(static_cast<std::uint32_t>(m.edges.size()));
    for (const wf::RoadEdge& e : m.edges) {
        w.write(e.a);
        w.write(e.b);
        w.write(static_cast<std::uint8_t>(e.extra ? 1 : 0));
        w.write(e.cost);
        w.write(e.pathBegin);
        w.write(e.pathCount);
    }
    w.write(m.pathCells);
}

void putPolitics(BinaryWriter& w, const wf::PoliticsMap& m)
{
    w.write(m.seed);
    w.write(static_cast<std::int32_t>(m.factionCount));
    w.write(m.owner);
    w.write(m.rival);
    w.write(static_cast<std::uint32_t>(m.homes.size()));
    for (const wf::PoliticsSite& h : m.homes) {
        w.write(h.x);
        w.write(h.y);
    }
    w.write(m.homeSite);
    w.write(m.siteOwner);
    w.write(m.territory);
    w.write(m.hostility);
}

void putPois(BinaryWriter& w, const wf::PoiMap& m)
{
    w.write(m.seed);
    w.write(static_cast<std::int32_t>(m.viewpointCount));
    w.write(static_cast<std::uint32_t>(m.pois.size()));
    for (const wf::Poi& p : m.pois) {
        w.write(p.x);
        w.write(p.y);
        w.write(p.score);
        w.write(p.visibleCount);
    }
}

void putCoverage(BinaryWriter& w, const wf::LandmarkCoverageReport& c)
{
    for (const int v : {c.stride, c.sampleCount, c.visibleCount, c.nearCount, c.coveredCount, c.coveragePermille}) {
        w.write(static_cast<std::int32_t>(v));
    }
    w.write(static_cast<std::uint8_t>(c.passed ? 1 : 0));
    w.write(static_cast<std::uint32_t>(c.gaps.size()));
    for (const wf::CoverageSample& g : c.gaps) {
        w.write(static_cast<std::int32_t>(g.x));
        w.write(static_cast<std::int32_t>(g.y));
    }
}

// A count read from the file is checked against the bytes left before anything is allocated.
bool readCount(BinaryReader& r, std::size_t bytesPerEntry, std::size_t& count)
{
    std::uint32_t n = 0;
    if (!r.read(n) || n > r.remaining() / bytesPerEntry) {
        return false;
    }
    count = n;
    return true;
}

bool getHydrology(BinaryReader& r, int size, wf::HydrologyMap& m)
{
    m.width = size;
    m.height = size;
    return r.read(m.seed) && r.read(m.filledM) && r.read(m.lakeDepthM) && r.read(m.heightM) && r.read(m.flowDir) && r.read(m.accumulation) && r.read(m.river);
}

bool getBiomes(BinaryReader& r, int size, wf::BiomeMap& m)
{
    m.width = size;
    m.height = size;
    return r.read(m.seed) && r.read(m.biome) && r.read(m.secondary) && r.read(m.blend);
}

bool getSettlements(BinaryReader& r, int size, wf::SettlementMap& m)
{
    m.width = size;
    m.height = size;
    std::size_t count = 0;
    if (!r.read(m.seed) || !r.read(m.suitability) || !readCount(r, 7, count)) {
        return false;
    }
    m.sites.resize(count);
    for (wf::Settlement& s : m.sites) {
        if (!r.read(s.x) || !r.read(s.y) || !r.read(s.score) || !r.read(s.tier) || !r.read(s.flags)) {
            return false;
        }
    }
    return true;
}

bool getRoads(BinaryReader& r, int size, wf::RoadMap& m)
{
    m.width = size;
    m.height = size;
    std::int32_t components = 0;
    std::size_t count = 0;
    if (!r.read(m.seed) || !r.read(components) || !r.read(m.heightM) || !r.read(m.road) || !readCount(r, 25, count)) {
        return false;
    }
    m.components = components;
    m.edges.resize(count);
    for (wf::RoadEdge& e : m.edges) {
        std::uint8_t extra = 0;
        if (!r.read(e.a) || !r.read(e.b) || !r.read(extra) || extra > 1 || !r.read(e.cost) || !r.read(e.pathBegin) || !r.read(e.pathCount)) {
            return false;
        }
        e.extra = extra == 1;
    }
    return r.read(m.pathCells);
}

bool getPolitics(BinaryReader& r, int size, wf::PoliticsMap& m)
{
    m.width = size;
    m.height = size;
    std::int32_t factions = 0;
    std::size_t count = 0;
    if (!r.read(m.seed) || !r.read(factions) || !r.read(m.owner) || !r.read(m.rival) || !readCount(r, 4, count)) {
        return false;
    }
    m.factionCount = factions;
    m.homes.resize(count);
    for (wf::PoliticsSite& h : m.homes) {
        if (!r.read(h.x) || !r.read(h.y)) {
            return false;
        }
    }
    return r.read(m.homeSite) && r.read(m.siteOwner) && r.read(m.territory) && r.read(m.hostility);
}

bool getPois(BinaryReader& r, int size, wf::PoiMap& m)
{
    m.width = size;
    m.height = size;
    std::int32_t viewpoints = 0;
    std::size_t count = 0;
    if (!r.read(m.seed) || !r.read(viewpoints) || !readCount(r, 8, count)) {
        return false;
    }
    m.viewpointCount = viewpoints;
    m.pois.resize(count);
    for (wf::Poi& p : m.pois) {
        if (!r.read(p.x) || !r.read(p.y) || !r.read(p.score) || !r.read(p.visibleCount)) {
            return false;
        }
    }
    return true;
}

bool getCoverage(BinaryReader& r, wf::LandmarkCoverageReport& c)
{
    std::int32_t values[6] = {};
    std::uint8_t passed = 0;
    std::size_t count = 0;
    for (std::int32_t& v : values) {
        if (!r.read(v) || v < 0) {
            return false;
        }
    }
    if (!r.read(passed) || passed > 1 || !readCount(r, 8, count)) {
        return false;
    }
    c.stride = values[0];
    c.sampleCount = values[1];
    c.visibleCount = values[2];
    c.nearCount = values[3];
    c.coveredCount = values[4];
    c.coveragePermille = values[5];
    c.passed = passed == 1;
    c.gaps.resize(count);
    for (wf::CoverageSample& g : c.gaps) {
        std::int32_t x = 0;
        std::int32_t y = 0;
        if (!r.read(x) || !r.read(y)) {
            return false;
        }
        g.x = x;
        g.y = y;
    }
    return true;
}

bool inGrid(int x, int y, int size)
{
    return x >= 0 && y >= 0 && x < size && y < size;
}

bool hydrologySound(const wf::HydrologyMap& m, std::size_t cells, int size)
{
    if (m.filledM.size() != cells || m.lakeDepthM.size() != cells || m.heightM.size() != cells || m.flowDir.size() != cells || m.accumulation.size() != cells
        || m.river.size() != cells) {
        return false;
    }
    for (std::size_t c = 0; c < cells; ++c) {
        const std::uint8_t dir = m.flowDir[c];
        if (dir == wf::kHydrologyNoFlow) {
            continue;
        }
        const int x = static_cast<int>(c % static_cast<std::size_t>(size));
        const int y = static_cast<int>(c / static_cast<std::size_t>(size));
        if (dir >= 8 || !inGrid(x + wf::detail::kHydrologyDx[dir], y + wf::detail::kHydrologyDy[dir], size)) {
            return false;
        }
    }
    return true;
}

bool biomesSound(const wf::BiomeMap& m, std::size_t cells)
{
    if (m.biome.size() != cells || m.secondary.size() != cells || m.blend.size() != cells) {
        return false;
    }
    for (std::size_t c = 0; c < cells; ++c) {
        if (m.biome[c] >= wf::kBiomeCount || m.secondary[c] >= wf::kBiomeCount) {
            return false;
        }
    }
    return true;
}

bool settlementsSound(const wf::SettlementMap& m, std::size_t cells, int size)
{
    if (m.suitability.size() != cells || m.sites.size() > static_cast<std::size_t>(wf::kSettlementMaxSites)) {
        return false;
    }
    for (const wf::Settlement& s : m.sites) {
        if (!inGrid(s.x, s.y, size) || s.tier > static_cast<std::uint8_t>(wf::SettlementTier::City)) {
            return false;
        }
    }
    return true;
}

bool roadsSound(const wf::RoadMap& m, std::size_t cells, std::size_t siteCount)
{
    if (m.heightM.size() != cells || m.road.size() != cells || m.components < 0 || m.pathCells.size() > wf::kRoadsMaxPathCells) {
        return false;
    }
    for (const std::uint32_t c : m.pathCells) {
        if (c >= cells) {
            return false;
        }
    }
    for (const std::uint8_t kind : m.road) {
        if (kind > 2) {
            return false;
        }
    }
    for (const wf::RoadEdge& e : m.edges) {
        if (e.a >= siteCount || e.b >= siteCount || e.cost < 0) {
            return false;
        }
        if (static_cast<std::uint64_t>(e.pathBegin) + e.pathCount > m.pathCells.size()) {
            return false;
        }
    }
    return true;
}

bool politicsSound(const wf::PoliticsMap& m, std::size_t cells, int size, std::size_t siteCount)
{
    if (m.factionCount < 0 || m.factionCount > wf::kPoliticsMaxFactions || m.owner.size() != cells || m.rival.size() != cells
        || m.homes.size() != static_cast<std::size_t>(m.factionCount) || m.homeSite.size() != m.homes.size() || m.territory.size() != m.homes.size()
        || m.siteOwner.size() != siteCount || m.hostility.size() != static_cast<std::size_t>(m.factionCount) * static_cast<std::size_t>(m.factionCount)) {
        return false;
    }
    for (std::size_t c = 0; c < cells; ++c) {
        if ((m.owner[c] != wf::kPoliticsNoOwner && m.owner[c] >= m.factionCount) || (m.rival[c] != wf::kPoliticsNoOwner && m.rival[c] >= m.factionCount)) {
            return false;
        }
    }
    for (std::size_t f = 0; f < m.homes.size(); ++f) {
        if (!inGrid(m.homes[f].x, m.homes[f].y, size) || m.homeSite[f] < -1 || m.homeSite[f] >= static_cast<std::int64_t>(siteCount)) {
            return false;
        }
    }
    for (const std::uint8_t owner : m.siteOwner) {
        if (owner != wf::kPoliticsNoOwner && owner >= m.factionCount) {
            return false;
        }
    }
    return true;
}

bool poisSound(const wf::PoiMap& m, std::size_t cells, int size)
{
    if (m.viewpointCount < 0 || m.pois.size() > cells) {
        return false;
    }
    for (const wf::Poi& p : m.pois) {
        if (!inGrid(p.x, p.y, size)) {
            return false;
        }
    }
    return true;
}

}

std::uint64_t chainForgeHashAt(std::uint64_t seed, std::uint32_t genVersion, int size, const PhaseHashes& phaseHashes, PhaseHashes* chain)
{
    std::uint64_t h = wf::splitmix64(seed ^ wf::detail::kForgeDomain);
    h = wf::splitmix64(h ^ genVersion);
    h = wf::splitmix64(h ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(size)));
    for (std::size_t i = 0; i < phaseHashes.size(); ++i) {
        h = wf::detail::forgeChainStep(h, static_cast<wf::ForgePhase>(i + 1), phaseHashes[i]);
        if (chain != nullptr) {
            (*chain)[i] = h;
        }
    }
    return h;
}

std::vector<std::uint8_t> encodeForgedPhaseMaps(const ForgedWorldKey& key, const wf::ForgedWorld& world)
{
    if (!world.hydrology() || !world.biomes() || !world.settlements() || !world.roads() || !world.politics() || !world.pois() || !world.landmarkCoverage()
        || !world.manifest() || !world.complete() || world.seed() != key.seed || world.size() != key.gridSize) {
        return {};
    }
    BinaryWriter w;
    w.write(kMagic);
    w.write(kVersion);
    w.write(key.seed);
    w.write(key.genVersion);
    w.write(key.gridSize);
    w.write(world.worldHash());
    for (const std::uint64_t hash : world.phaseHashes()) {
        w.write(hash);
    }
    putHydrology(w, *world.hydrology());
    putBiomes(w, *world.biomes());
    putSettlements(w, *world.settlements());
    putRoads(w, *world.roads());
    putPolitics(w, *world.politics());
    putPois(w, *world.pois());
    putCoverage(w, *world.landmarkCoverage());
    std::vector<std::uint8_t> bytes = w.take();
    BinaryWriter tail;
    tail.write(checksum(bytes.data(), bytes.size()));
    bytes.insert(bytes.end(), tail.bytes().begin(), tail.bytes().end());
    return bytes;
}

std::shared_ptr<const wf::ForgedWorld> decodeForgedPhaseMaps(std::span<const std::uint8_t> bytes, const ForgedWorldKey& key, const CachedForgedWorld& stored)
{
    if (bytes.size() < kSumBytes || key.gridSize < wf::kTectonicsMinSize || key.gridSize > wf::kTectonicsMaxSize) {
        return nullptr;
    }
    constexpr std::size_t kBytesPerCell = 40;
    if (bytes.size() > cellCount(key.gridSize) * kBytesPerCell + wf::kRoadsMaxPathCells * sizeof(std::uint32_t) + (1u << 20)) {
        return nullptr;
    }
    const std::size_t bodySize = bytes.size() - kSumBytes;
    std::uint64_t sum = 0;
    std::memcpy(&sum, bytes.data() + bodySize, kSumBytes);
    if (sum != checksum(bytes.data(), bodySize)) {
        return nullptr;
    }

    const int size = key.gridSize;
    const std::size_t cells = cellCount(size);
    BinaryReader r(bytes.first(bodySize));
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint64_t seed = 0;
    std::uint32_t genVersion = 0;
    std::int32_t stampedSize = 0;
    wf::detail::ForgedParts parts;
    if (!r.read(magic) || magic != kMagic || !r.read(version) || version != kVersion || !r.read(seed) || seed != key.seed || !r.read(genVersion)
        || genVersion != key.genVersion || !r.read(stampedSize) || stampedSize != size || !r.read(parts.worldHash) || parts.worldHash != stored.worldHash) {
        return nullptr;
    }
    for (std::uint64_t& hash : parts.phaseHashes) {
        if (!r.read(hash)) {
            return nullptr;
        }
    }
    wf::HydrologyMap hydrology;
    wf::BiomeMap biomes;
    wf::SettlementMap settlements;
    wf::RoadMap roads;
    wf::PoliticsMap politics;
    wf::PoiMap pois;
    wf::LandmarkCoverageReport coverage;
    if (!getHydrology(r, size, hydrology) || !getBiomes(r, size, biomes) || !getSettlements(r, size, settlements) || !getRoads(r, size, roads)
        || !getPolitics(r, size, politics) || !getPois(r, size, pois) || !getCoverage(r, coverage) || !r.atEnd()) {
        return nullptr;
    }

    if (!hydrologySound(hydrology, cells, size) || !biomesSound(biomes, cells) || !settlementsSound(settlements, cells, size)
        || !roadsSound(roads, cells, settlements.sites.size()) || !politicsSound(politics, cells, size, settlements.sites.size()) || !poisSound(pois, cells, size)) {
        return nullptr;
    }
    const std::uint64_t chained = chainForgeHashAt(key.seed, key.genVersion, size, parts.phaseHashes, &parts.chain);
    // The hash functions are this build's. A world stored by an older generator may have been hashed by code that has since
    // changed, so it is held to the checksum, the stamps and the soundness checks above, and loads as stored.
    if (key.genVersion == wf::kGenVersion) {
        const std::uint64_t poiHash = wf::splitmix64(wf::hashPoiMap(pois) ^ wf::splitmix64(wf::hashLandmarkCoverage(coverage)));
        if (wf::hashHydrologyMap(hydrology) != parts.phaseHashes[kHydrologyHash] || wf::hashBiomeMap(biomes) != parts.phaseHashes[kBiomesHash]
            || wf::hashSettlementMap(settlements) != parts.phaseHashes[kSettlementsHash] || wf::hashRoadMap(roads) != parts.phaseHashes[kRoadsHash]
            || wf::hashPoliticsMap(politics) != parts.phaseHashes[kPoliticsHash] || poiHash != parts.phaseHashes[kPoisHash]
            || wf::hashManifest(stored.manifest) != parts.phaseHashes[kContentHash] || stored.manifest.poiIds.size() != settlements.sites.size() + pois.pois.size()
            || chained != parts.worldHash) {
            return nullptr;
        }
    }

    parts.seed = key.seed;
    parts.size = size;
    parts.stopAfter = wf::ForgePhase::Content;
    parts.completed = wf::kForgePhaseCount;
    parts.hydrology = std::move(hydrology);
    parts.biomes = std::move(biomes);
    parts.settlements = std::move(settlements);
    parts.roads = std::move(roads);
    parts.politics = std::move(politics);
    parts.pois = std::move(pois);
    parts.landmarkCoverage = std::move(coverage);
    parts.manifest = stored.manifest;
    return std::make_shared<const wf::ForgedWorld>(std::move(parts));
}

}
