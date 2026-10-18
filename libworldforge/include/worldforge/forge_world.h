#pragma once

#include <worldforge/biome_map.h>
#include <worldforge/climate_map.h>
#include <worldforge/content_manifest.h>
#include <worldforge/erosion_budget.h>
#include <worldforge/erosion_map.h>
#include <worldforge/gen_version.h>
#include <worldforge/hydrology_map.h>
#include <worldforge/landmark_coverage.h>
#include <worldforge/poi_map.h>
#include <worldforge/politics_map.h>
#include <worldforge/resource_map.h>
#include <worldforge/road_map.h>
#include <worldforge/settlement_map.h>
#include <worldforge/subseed.h>
#include <worldforge/tectonics_map.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The forge runner (doc 08, "Shape: a batch compiler"): forgeWorld runs phases 1 to 11 in order,
// handing each phase's real output to the next, and returns an immutable ForgedWorld with one
// field per phase. Phase 0 (axioms) is not here: the caller resolves axioms into a ForgeConfig, and
// the axiom to params mapping is a separate piece of work. Phase 12 (bake) needs the engine.
//
// Wiring between phases, including the optional inputs earlier phases left open:
//  - Hydrology, Climate: from the eroded map. Biomes: climate + hydrology.
//  - Resources: tectonics + eroded, with the climate moisture scaled into its byte input
//    (moisture * 255 / moistureScale, clamped). Hydrology does not feed Resources: that phase has
//    no water input yet, so water proximity stays deferred there.
//  - Settlements: hydrology + climate + resources, with biome suitability looked up per cell from
//    ForgeConfig::biomeSuitability.
//  - Roads: hydrology, the settlement sites as road sites, and a biome cost byte per cell from
//    ForgeConfig::biomeRoadCost (255 is impassable). Roads own the deformed terrain.
//  - Politics: eroded + resources + settlement sites + ForgeConfig::factions.
//  - POIs: the road map (deformed terrain) and the settlements; landmark coverage is then measured
//    over the POIs as landmarks of ForgeConfig::landmarkHeightM on the roads' terrain. The report
//    is carried, never used as a gate (validation gates and reroll belong to the caller). Only the
//    first kLandmarkMaxLandmarks (128) POIs, which are the best scored, are measured.
//  - Content: settlements and POIs become PoiInputs with ids "settlement_NNNN" and "poi_NNNN".
//    A POI is a Dungeon (when dungeonArchetypes is non-empty), a Ruin or a Landmark by a hash of
//    its cell and the world seed.
//
// Failure: the first phase whose generator rejects its input or params stops the run. The result
// names that phase, carries a message, and keeps every earlier phase's artifact. Nothing is rerolled.
//
// World hash: a chain. It starts from (seed, genVersion, size) and folds in, per completed phase in
// order, the phase number and that phase's artifact hash. Dropping, reordering or changing any
// phase changes every link after it. ForgedWorld::chain()[i] is the chain after phase i + 1, so a
// run stopped after phase N has the same worldHash as chain()[N - 1] of a full run.
//
// Size: a square grid of `size` cells per side, within [kTectonicsMinSize, kTectonicsMaxSize]
// (default 256). The erosion cost is checked against ForgeConfig::erosionBudgetCellPasses with
// planErosionBudget; a size that would need downscaled erosion is refused at Erosion, because the
// runner has no upsampling step.
//
// Determinism: integer only in this file, no libm, no unordered container, no threads, no engine
// includes. The progress callback must not mutate shared generator state; it observes only.
namespace worldforge {

enum class ForgePhase : std::uint8_t {
    Tectonics = 1,
    Erosion = 2,
    Hydrology = 3,
    Climate = 4,
    Biomes = 5,
    Resources = 6,
    Settlements = 7,
    Roads = 8,
    Politics = 9,
    Pois = 10,
    Content = 11,
};

constexpr int kForgePhaseCount = 11;
constexpr int kForgeDefaultSize = 256;

constexpr std::string_view forgePhaseName(ForgePhase phase)
{
    switch (phase) {
    case ForgePhase::Tectonics: return "tectonics";
    case ForgePhase::Erosion: return "erosion";
    case ForgePhase::Hydrology: return "hydrology";
    case ForgePhase::Climate: return "climate";
    case ForgePhase::Biomes: return "biomes";
    case ForgePhase::Resources: return "resources";
    case ForgePhase::Settlements: return "settlements";
    case ForgePhase::Roads: return "roads";
    case ForgePhase::Politics: return "politics";
    case ForgePhase::Pois: return "pois";
    case ForgePhase::Content: return "content";
    }
    return "unknown";
}

// Indexed by BiomeId. 0 hostile to 255 ideal for a settlement.
constexpr std::array<std::uint8_t, kBiomeCount> kDefaultBiomeSuitability = {
    0, 0, 10, 60, 110, 160, 220, 40, 150, 90, 50, 20, 70, 255, 20, 40,
};

// Indexed by BiomeId. Extra road cost per cell; kRoadsImpassable (255) blocks the cell.
constexpr std::array<std::uint8_t, kBiomeCount> kDefaultBiomeRoadCost = {
    255, 255, 80, 20, 30, 30, 0, 25, 10, 45, 60, 90, 70, 0, 40, 50,
};

struct ForgeConfig {
    TectonicsParams tectonics; // width and height are replaced by the requested size
    ErosionParams erosion;
    std::int64_t erosionBudgetCellPasses = kErosionMaxCellPasses;
    HydrologyParams hydrology;
    ClimateParams climate;
    BiomeParams biomes;
    ResourceParams resources;
    int moistureScale = 24; // climate moisture that maps to a full wet byte, in [1, kClimateMaxMoisture]
    SettlementParams settlements;
    std::array<std::uint8_t, kBiomeCount> biomeSuitability = kDefaultBiomeSuitability;
    RoadParams roads;
    std::array<std::uint8_t, kBiomeCount> biomeRoadCost = kDefaultBiomeRoadCost;
    PoliticsParams politics;
    std::vector<FactionSpec> factions;
    PoiParams pois;
    int landmarkHeightM = 24; // silhouette height given to every POI, in [1, kLandmarkMaxHeightM]
    LandmarkCoverageParams landmarks;
    BindingParams binding;
    std::vector<std::string> itemIds;
    std::vector<std::string> npcArchetypes;
    std::vector<std::string> dungeonArchetypes;
};

struct ForgeProgress {
    ForgePhase phase = ForgePhase::Tectonics;
    bool finished = false;       // false when the phase is about to run, true once it ended
    bool ok = true;              // meaningful when finished
    std::uint64_t phaseHash = 0; // artifact hash, meaningful when finished and ok
};

using ForgeProgressFn = std::function<void(const ForgeProgress&)>;

struct ForgePhaseHash {
    ForgePhase phase = ForgePhase::Tectonics;
    std::uint64_t hash = 0;
};

namespace detail {

constexpr std::uint64_t kForgeDomain = 0x666f7267655f7731ull;

constexpr std::uint64_t forgeChainStart(std::uint64_t seed, int size)
{
    std::uint64_t h = splitmix64(seed ^ kForgeDomain);
    h = splitmix64(h ^ kGenVersion);
    return splitmix64(h ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(size)));
}

constexpr std::uint64_t forgeChainStep(std::uint64_t chain, ForgePhase phase, std::uint64_t phaseHash)
{
    return splitmix64(splitmix64(chain ^ splitmix64(static_cast<std::uint64_t>(phase))) ^ phaseHash);
}

}

// The world hash for a run that completed exactly these phases, in this order.
constexpr std::uint64_t chainForgeHash(std::uint64_t seed, int size, std::span<const ForgePhaseHash> phases)
{
    std::uint64_t h = detail::forgeChainStart(seed, size);
    for (const ForgePhaseHash& p : phases) h = detail::forgeChainStep(h, p.phase, p.hash);
    return h;
}


namespace detail {

struct ForgedParts {
    std::uint64_t seed = 0;
    int size = 0;
    ForgePhase stopAfter = ForgePhase::Content;
    int completed = 0;
    std::optional<ForgePhase> failedPhase;
    std::string error;
    std::uint64_t worldHash = 0;
    std::array<std::uint64_t, kForgePhaseCount> phaseHashes{};
    std::array<std::uint64_t, kForgePhaseCount> chain{};
    std::optional<TectonicMap> tectonics;
    std::optional<ErodedMap> eroded;
    std::optional<HydrologyMap> hydrology;
    std::optional<ClimateMap> climate;
    std::optional<BiomeMap> biomes;
    std::optional<ResourceMap> resources;
    std::optional<SettlementMap> settlements;
    std::optional<RoadMap> roads;
    std::optional<PoliticsMap> politics;
    std::optional<PoiMap> pois;
    std::optional<LandmarkCoverageReport> landmarkCoverage;
    std::optional<ContentManifest> manifest;
};

}

// Built only by forgeWorld; every accessor is const, so a ForgedWorld cannot change after the run.
// A field is empty when its phase did not run (stopped earlier or failed).
class ForgedWorld {
public:
    explicit ForgedWorld(detail::ForgedParts parts) : p_(std::move(parts)) {}

    std::uint64_t seed() const { return p_.seed; }
    int size() const { return p_.size; }
    std::uint32_t genVersion() const { return kGenVersion; }
    ForgePhase stopAfter() const { return p_.stopAfter; }

    // Number of phases that ran to completion (0 to kForgePhaseCount).
    int completedPhases() const { return p_.completed; }
    bool ok() const { return !p_.failedPhase.has_value(); }
    // True when the run reached its stopAfter phase without failing.
    bool complete() const { return ok() && p_.completed == static_cast<int>(p_.stopAfter); }
    std::optional<ForgePhase> failedPhase() const { return p_.failedPhase; }
    const std::string& error() const { return p_.error; }

    // Chained hash over the completed phases; see chainForgeHash.
    std::uint64_t worldHash() const { return p_.worldHash; }
    // Artifact hash of phase i + 1, 0 when it did not complete.
    const std::array<std::uint64_t, kForgePhaseCount>& phaseHashes() const { return p_.phaseHashes; }
    // Chain after phase i + 1, 0 when it did not complete.
    const std::array<std::uint64_t, kForgePhaseCount>& chain() const { return p_.chain; }

    const std::optional<TectonicMap>& tectonics() const { return p_.tectonics; }
    const std::optional<ErodedMap>& eroded() const { return p_.eroded; }
    const std::optional<HydrologyMap>& hydrology() const { return p_.hydrology; }
    const std::optional<ClimateMap>& climate() const { return p_.climate; }
    const std::optional<BiomeMap>& biomes() const { return p_.biomes; }
    const std::optional<ResourceMap>& resources() const { return p_.resources; }
    const std::optional<SettlementMap>& settlements() const { return p_.settlements; }
    // Carries the road-deformed terrain (RoadMap::heightM).
    const std::optional<RoadMap>& roads() const { return p_.roads; }
    const std::optional<PoliticsMap>& politics() const { return p_.politics; }
    const std::optional<PoiMap>& pois() const { return p_.pois; }
    // Measured over the POIs with the roads' terrain; the numbers a validation gate reads.
    const std::optional<LandmarkCoverageReport>& landmarkCoverage() const { return p_.landmarkCoverage; }
    // Stamped with kGenVersion (manifest version kManifestVersionGen).
    const std::optional<ContentManifest>& manifest() const { return p_.manifest; }

private:
    detail::ForgedParts p_;
};

namespace detail {

constexpr std::uint64_t kForgePoiDomain = 0x666f7267655f706full;

inline std::string forgeSettlementId(std::size_t i) { return "settlement_" + std::string(4 - std::min<std::size_t>(4, std::to_string(i).size()), '0') + std::to_string(i); }
inline std::string forgePoiId(std::size_t i) { return "poi_" + std::string(4 - std::min<std::size_t>(4, std::to_string(i).size()), '0') + std::to_string(i); }

inline std::vector<std::uint8_t> forgeLookup(const std::vector<std::uint8_t>& biome, const std::array<std::uint8_t, kBiomeCount>& table)
{
    std::vector<std::uint8_t> out(biome.size());
    for (std::size_t c = 0; c < biome.size(); ++c) out[c] = table[biome[c] % kBiomeCount];
    return out;
}

inline std::vector<std::uint8_t> forgeMoistureBytes(const ClimateMap& climate, int scale)
{
    std::vector<std::uint8_t> out(climate.moisture.size());
    for (std::size_t c = 0; c < out.size(); ++c) {
        out[c] = static_cast<std::uint8_t>(std::min<std::int64_t>(255, static_cast<std::int64_t>(climate.moisture[c]) * 255 / scale));
    }
    return out;
}

inline BindingInputs forgeBindingInputs(std::uint64_t seed, const SettlementMap& settlements, const PoiMap& pois, const ForgeConfig& cfg)
{
    BindingInputs in;
    in.worldSeed = seed;
    in.itemIds = cfg.itemIds;
    in.npcArchetypes = cfg.npcArchetypes;
    for (std::size_t i = 0; i < settlements.sites.size(); ++i) in.pois.push_back({ forgeSettlementId(i), PoiKind::Settlement, {} });
    const std::uint64_t salt = splitmix64(seed ^ kForgePoiDomain);
    for (std::size_t i = 0; i < pois.pois.size(); ++i) {
        const Poi& p = pois.pois[i];
        const std::uint64_t pick = splitmix64(salt ^ hashCell(p.x, p.y));
        PoiInput poi;
        poi.id = forgePoiId(i);
        switch (pick % 3) {
        case 0:
            if (!cfg.dungeonArchetypes.empty()) {
                poi.kind = PoiKind::Dungeon;
                poi.archetype = cfg.dungeonArchetypes[static_cast<std::size_t>((pick >> 8) % cfg.dungeonArchetypes.size())];
            } else {
                poi.kind = PoiKind::Ruin;
            }
            break;
        case 1: poi.kind = PoiKind::Ruin; break;
        default: poi.kind = PoiKind::Landmark; break;
        }
        in.pois.push_back(std::move(poi));
    }
    return in;
}

}

// Runs phases 1 to `stopAfter` on a square grid of `size` cells per side. See the file comment.
// `progress`, when set, is called with finished = false before each phase and finished = true
// after it (also for the failing one).
inline ForgedWorld forgeWorld(std::uint64_t seed, int size = kForgeDefaultSize, const ForgeConfig& config = {}, ForgePhase stopAfter = ForgePhase::Content,
    const ForgeProgressFn& progress = {})
{
    detail::ForgedParts w;
    w.seed = seed;
    w.size = size;
    w.stopAfter = static_cast<ForgePhase>(std::clamp(static_cast<int>(stopAfter), 0, kForgePhaseCount));
    std::uint64_t chain = detail::forgeChainStart(seed, size);

    const auto step = [&](ForgePhase phase, auto&& body) -> bool {
        if (w.failedPhase.has_value() || static_cast<int>(phase) > static_cast<int>(stopAfter)) return false;
        if (progress) progress(ForgeProgress{ phase, false, true, 0 });
        std::string error;
        const std::optional<std::uint64_t> hash = body(error);
        if (!hash.has_value()) {
            w.failedPhase = phase;
            w.error = std::string(forgePhaseName(phase)) + ": " + error;
            if (progress) progress(ForgeProgress{ phase, true, false, 0 });
            return false;
        }
        const auto index = static_cast<std::size_t>(phase) - 1;
        chain = detail::forgeChainStep(chain, phase, *hash);
        w.phaseHashes[index] = *hash;
        w.chain[index] = chain;
        w.worldHash = chain;
        w.completed = static_cast<int>(phase);
        if (progress) progress(ForgeProgress{ phase, true, true, *hash });
        return true;
    };
    w.worldHash = chain;

    const std::string rejected = "rejected its inputs or params";

    // The hash type is optional so a body can fail with a message.
    using Hash = std::optional<std::uint64_t>;

    step(ForgePhase::Tectonics, [&](std::string& err) -> Hash {
        TectonicsParams params = config.tectonics;
        params.width = size;
        params.height = size;
        w.tectonics = generateTectonics(seed, params);
        if (!w.tectonics) {
            err = "size " + std::to_string(size) + " or plate count " + std::to_string(params.plateCount) + " is outside the supported range";
            return std::nullopt;
        }
        return hashTectonicMap(*w.tectonics);
    })
        && step(ForgePhase::Erosion, [&](std::string& err) -> Hash {
               const auto plan = planErosionBudget(size, size, config.erosion, config.erosionBudgetCellPasses);
               if (!plan) {
                   err = "erosion params or budget out of range";
                   return std::nullopt;
               }
               if (!plan->fits || plan->downscaleShift != 0 || plan->params.iterations != config.erosion.iterations) {
                   err = "size " + std::to_string(size) + " exceeds the erosion budget of " + std::to_string(config.erosionBudgetCellPasses) + " cell passes";
                   return std::nullopt;
               }
               w.eroded = erodeTectonics(*w.tectonics, config.erosion);
               if (!w.eroded) {
                   err = rejected;
                   return std::nullopt;
               }
               return hashErodedMap(*w.eroded);
           })
        && step(ForgePhase::Hydrology, [&](std::string& err) -> Hash {
               w.hydrology = computeHydrology(*w.eroded, config.hydrology);
               if (!w.hydrology) {
                   err = rejected;
                   return std::nullopt;
               }
               return hashHydrologyMap(*w.hydrology);
           })
        && step(ForgePhase::Climate, [&](std::string& err) -> Hash {
               w.climate = generateClimate(*w.eroded, config.climate);
               if (!w.climate) {
                   err = rejected;
                   return std::nullopt;
               }
               return hashClimateMap(*w.climate);
           })
        && step(ForgePhase::Biomes, [&](std::string& err) -> Hash {
               w.biomes = generateBiomes(BiomeInputs{ &*w.climate, &*w.hydrology }, config.biomes);
               if (!w.biomes) {
                   err = rejected;
                   return std::nullopt;
               }
               return hashBiomeMap(*w.biomes);
           })
        && step(ForgePhase::Resources, [&](std::string& err) -> Hash {
               if (config.moistureScale < 1 || config.moistureScale > kClimateMaxMoisture) {
                   err = "moistureScale is outside [1, " + std::to_string(kClimateMaxMoisture) + "]";
                   return std::nullopt;
               }
               const std::vector<std::uint8_t> moisture = detail::forgeMoistureBytes(*w.climate, config.moistureScale);
               w.resources = generateResources(ResourceInputs{ &*w.tectonics, &*w.eroded, moisture }, config.resources);
               if (!w.resources) {
                   err = rejected;
                   return std::nullopt;
               }
               return hashResourceMap(*w.resources);
           })
        && step(ForgePhase::Settlements, [&](std::string& err) -> Hash {
               const std::vector<std::uint8_t> suitability = detail::forgeLookup(w.biomes->biome, config.biomeSuitability);
               w.settlements = generateSettlements(SettlementInputs{ &*w.hydrology, &*w.climate, &*w.resources, suitability }, config.settlements);
               if (!w.settlements) {
                   err = rejected;
                   return std::nullopt;
               }
               return hashSettlementMap(*w.settlements);
           })
        && step(ForgePhase::Roads, [&](std::string& err) -> Hash {
               std::vector<RoadSite> sites;
               sites.reserve(w.settlements->sites.size());
               for (const Settlement& s : w.settlements->sites) sites.push_back({ s.x, s.y });
               const std::vector<std::uint8_t> cost = detail::forgeLookup(w.biomes->biome, config.biomeRoadCost);
               w.roads = computeRoads(*w.hydrology, sites, config.roads, cost);
               if (!w.roads) {
                   err = sites.empty() ? "no settlement sites to connect" : rejected;
                   return std::nullopt;
               }
               return hashRoadMap(*w.roads);
           })
        && step(ForgePhase::Politics, [&](std::string& err) -> Hash {
               std::vector<PoliticsSite> sites;
               sites.reserve(w.settlements->sites.size());
               for (const Settlement& s : w.settlements->sites) sites.push_back({ s.x, s.y });
               w.politics = generatePolitics(PoliticsInputs{ &*w.eroded, &*w.resources, sites, config.factions }, config.politics);
               if (!w.politics) {
                   err = rejected;
                   return std::nullopt;
               }
               return hashPoliticsMap(*w.politics);
           })
        && step(ForgePhase::Pois, [&](std::string& err) -> Hash {
               auto pois = placePois(*w.roads, &*w.settlements, config.pois);
               if (!pois) {
                   err = rejected;
                   return std::nullopt;
               }
               std::vector<Landmark> landmarks;
               const std::size_t count = std::min<std::size_t>(pois->pois.size(), kLandmarkMaxLandmarks);
               for (std::size_t i = 0; i < count; ++i) landmarks.push_back({ pois->pois[i].x, pois->pois[i].y, config.landmarkHeightM });
               const LandmarkCoverageInputs inputs{ w.roads->width, w.roads->height, w.roads->heightM, landmarks };
               auto coverage = validateLandmarkCoverage(inputs, config.landmarks);
               if (!coverage) {
                   err = "landmark coverage " + rejected;
                   return std::nullopt;
               }
               w.pois = std::move(pois);
               w.landmarkCoverage = std::move(coverage);
               return splitmix64(hashPoiMap(*w.pois) ^ splitmix64(hashLandmarkCoverage(*w.landmarkCoverage)));
           })
        && step(ForgePhase::Content, [&](std::string& err) -> Hash {
               w.manifest = bindContent(detail::forgeBindingInputs(seed, *w.settlements, *w.pois, config), config.binding);
               if (!w.manifest) {
                   err = rejected;
                   return std::nullopt;
               }
               w.manifest->version = kManifestVersionGen;
               w.manifest->genVersion = kGenVersion;
               return hashManifest(*w.manifest);
           });

    return ForgedWorld(std::move(w));
}

}
