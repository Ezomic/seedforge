#pragma once

#include <worldforge/forge_world.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// World sanity report (doc 08, validation gates): scores a ForgedWorld against the design promises
// and, optionally, rerolls the seed until a world passes.
//
// validateWorld(world, thresholds) returns one GateResult per gate, in this fixed order:
//  - settlement_density:   settlements per square km, times 1000, inside [min, max].
//  - road_connected:       road graph components, at most maxRoadComponents (1 = every site on one network).
//  - settlement_reach:     permille of settlements in the largest walkable group of settlements, at least minSettlementReachPermille.
//  - no_dead_ends:         permille of POIs inside that same walkable group, at least minPoiReachPermille.
//  - landmark_coverage:    the landmark coverage report's permille, at least minLandmarkCoveragePermille.
//  - quest_items:          items the manifest placed, at least minQuestItems.
//  - quest_cast:           cast members the manifest bound to a settlement, at least minQuestCast.
//  - land_share:           permille of cells whose terrain is above seaLevelM, inside [minLandSharePermille, maxLandSharePermille].
//  - axiom_directives_starved: skipped, reported as not implemented (axioms are not resolved yet).
//
// Reach is plain walkability; the walkable cells fall into groups, the largest group of settlements
// is the main area, and POIs outside it are dead ends. Walkability is plain slope and water (traversal verbs are a later
// refinement). A cell is walkable
// when the road map's terrain is above seaLevelM, it is not a lake, and it is not a river cell
// unless a road crosses it (ford or bridge). A step between 8-connected walkable cells is allowed
// when the rise over the run is at most maxWalkSlopePermille, with the run cellSizeM orthogonally and
// cellSizeM * 1414 / 1000 diagonally. cellSizeM and seaLevelM must match the ForgeConfig the world
// was forged with; the world does not carry its config.
//
// Thresholds live in GateThresholds with their defaults. The defaults were measured by forging seeds
// 1 to 300 at size 256 with a default ForgeConfig (plus four items, two NPC archetypes and two dungeon
// archetypes) and picking the gate values that reject about 5 percent of them (16 of 300). The default
// config makes fragmented worlds, so the measured thresholds are lenient (for example, one road
// network is the design promise but only 18 percent of those seeds have one); tighten them as the
// axiom to params mapping lands. Rerun the measurement whenever a phase or the default config changes.
//
// A world that did not complete, or whose artifacts are missing, mis-sized or hold a site outside the
// grid, fails every evaluated gate with the reason as cause. Invalid
// thresholds make the report carry an error, and the report never passes.
//
// Reroll: forgeValidated forges attempt 0 with the world seed itself, attempt n >= 1 with
// rerollSeed(worldSeed, n), validating each, up to maxAttempts (hard cap kValidateMaxAttempts = 8). It
// stops at the first world that passes. Every attempt keeps its seed, world hash and report, so the
// cause of each rejection is logged. When no attempt passes the last world is returned and the
// result is marked rejected, so the caller can still write its layers.
//
// The report is a pure read of the ForgedWorld and is never folded into worldHash. Determinism:
// integer only, no libm, no unordered container. The walk visits each cell at most once.
namespace worldforge {

constexpr int kValidateMaxAttempts = 8;
constexpr std::int64_t kGateUnbounded = std::numeric_limits<std::int64_t>::max();

enum class GateStatus : std::uint8_t { Pass, Fail, Skipped };

constexpr std::string_view gateStatusName(GateStatus s)
{
    switch (s) {
    case GateStatus::Pass: return "pass";
    case GateStatus::Fail: return "fail";
    case GateStatus::Skipped: return "skipped";
    }
    return "unknown";
}

struct GateThresholds {
    int cellSizeM = 16;                    // metres per cell, in [1, 1024]; match RoadParams::cellSizeM
    int seaLevelM = 0;                     // cells at or below this are water; match the forge config
    int maxWalkSlopePermille = 3000;       // steepest walkable step, rise over run in permille, in [0, 100000]
    int minSettlementsPerKm2Milli = 500;   // settlements per square km times 1000, lower bound
    int maxSettlementsPerKm2Milli = 20000; // and upper bound
    int maxRoadComponents = 5;             // in [1, 256]
    int minSettlementReachPermille = 250;  // in [0, 1000]
    int minPoiReachPermille = 150;        // in [0, 1000]
    int minLandmarkCoveragePermille = 550; // in [0, 1000]
    int minQuestItems = 1;                 // in [0, 65536]
    int minQuestCast = 1;                  // in [0, 65536]
    int minLandSharePermille = 100;        // in [0, 1000]; seeds 4 and 5 forge 4 to 9 percent land and pass every other gate
    int maxLandSharePermille = 800;        // in [minLandSharePermille, 1000]; the 16 seed range at size 1024 is 9 to 86 percent, mean 38
};

struct GateResult {
    std::string id;
    GateStatus status = GateStatus::Fail;
    std::int64_t value = 0;
    std::int64_t min = 0;
    std::int64_t max = kGateUnbounded; // kGateUnbounded when there is no upper bound
    std::string cause;                 // empty on pass
};

constexpr std::int64_t kLandShareNotMeasured = -1; // the land_share value of a world that could not be scored

struct ValidationReport {
    std::uint64_t seed = 0;
    std::uint64_t worldHash = 0;
    std::string error; // thresholds invalid; no gate was evaluated
    std::vector<GateResult> gates;

    bool passed() const
    {
        return error.empty() && !gates.empty() && std::none_of(gates.begin(), gates.end(), [](const GateResult& g) { return g.status == GateStatus::Fail; });
    }
};

// The land share in permille a report measured, or kLandShareNotMeasured when the world was unusable or the report has no such gate.
inline std::int64_t landSharePermille(const ValidationReport& r)
{
    const auto it = std::find_if(r.gates.begin(), r.gates.end(), [](const GateResult& g) { return g.id == "land_share"; });
    return it == r.gates.end() ? kLandShareNotMeasured : it->value;
}

inline bool thresholdsValid(const GateThresholds& t)
{
    const auto permille = [](int v) { return v >= 0 && v <= 1000; };
    return t.cellSizeM >= 1 && t.cellSizeM <= 1024 && t.seaLevelM >= kTectonicsMinHeightM && t.seaLevelM <= kTectonicsMaxHeightM && t.maxWalkSlopePermille >= 0
        && t.maxWalkSlopePermille <= 100000 && t.minSettlementsPerKm2Milli >= 0 && t.maxSettlementsPerKm2Milli >= t.minSettlementsPerKm2Milli && t.maxRoadComponents >= 1
        && t.maxRoadComponents <= 256 && permille(t.minSettlementReachPermille) && permille(t.minPoiReachPermille) && permille(t.minLandmarkCoveragePermille)
        && t.minQuestItems >= 0 && t.minQuestItems <= 65536 && t.minQuestCast >= 0 && t.minQuestCast <= 65536 && permille(t.minLandSharePermille)
        && permille(t.maxLandSharePermille) && t.maxLandSharePermille >= t.minLandSharePermille;
}

namespace detail {

inline std::string validationShapeError(const ForgedWorld& w)
{
    if (!w.settlements() || !w.pois() || !w.roads() || !w.hydrology() || !w.landmarkCoverage() || !w.manifest()) return "a phase artifact is missing";
    const int n = w.size();
    if (n < kTectonicsMinSize || n > kTectonicsMaxSize) return "world size is outside the supported range";
    const RoadMap& roads = *w.roads();
    const HydrologyMap& hydro = *w.hydrology();
    const auto cells = static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
    const bool sized = roads.width == n && roads.height == n && hydro.width == n && hydro.height == n && roads.heightM.size() == cells && roads.road.size() == cells
        && hydro.lakeDepthM.size() == cells && hydro.river.size() == cells;
    if (!sized) return "road or hydrology layers do not match the world size";
    const auto inside = [n](int x, int y) { return x >= 0 && y >= 0 && x < n && y < n; };
    for (const Settlement& s : w.settlements()->sites) {
        if (!inside(s.x, s.y)) return "a settlement lies outside the world";
    }
    for (const Poi& p : w.pois()->pois) {
        if (!inside(p.x, p.y)) return "a point of interest lies outside the world";
    }
    return {};
}

inline std::vector<std::uint32_t> validationWalkComponents(const ForgedWorld& w, const GateThresholds& t)
{
    const RoadMap& roads = *w.roads();
    const HydrologyMap& hydro = *w.hydrology();
    const int width = roads.width;
    const int height = roads.height;
    const std::size_t cells = roads.heightM.size();

    std::vector<std::uint8_t> walkable(cells, 0);
    for (std::size_t c = 0; c < cells; ++c) {
        const bool dry = roads.heightM[c] > t.seaLevelM && hydro.lakeDepthM[c] == 0;
        walkable[c] = (dry && (hydro.river[c] == 0 || roads.road[c] != 0)) ? 1 : 0;
    }

    std::vector<std::uint32_t> label(cells, 0);
    std::vector<std::uint32_t> queue;
    std::uint32_t next = 0;
    for (std::size_t start = 0; start < cells; ++start) {
        if (!walkable[start] || label[start] != 0) continue;
        ++next;
        queue.clear();
        queue.push_back(static_cast<std::uint32_t>(start));
        label[start] = next;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const std::size_t c = queue[head];
            const int cx = static_cast<int>(c % static_cast<std::size_t>(width));
            const int cy = static_cast<int>(c / static_cast<std::size_t>(width));
            for (int d = 0; d < 8; ++d) {
                const int nx = cx + kHydrologyDx[d];
                const int ny = cy + kHydrologyDy[d];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                const std::size_t n = static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx);
                if (label[n] != 0 || !walkable[n]) continue;
                const std::int64_t rise = std::abs(static_cast<std::int64_t>(roads.heightM[n]) - roads.heightM[c]);
                const std::int64_t run = (d % 2 == 0) ? t.cellSizeM : static_cast<std::int64_t>(t.cellSizeM) * 1414 / 1000;
                if (rise * 1000 > static_cast<std::int64_t>(t.maxWalkSlopePermille) * run) continue;
                label[n] = next;
                queue.push_back(static_cast<std::uint32_t>(n));
            }
        }
    }
    return label;
}

inline GateResult gateAtLeast(std::string id, std::int64_t value, std::int64_t min, std::string failCause)
{
    GateResult g;
    g.id = std::move(id);
    g.value = value;
    g.min = min;
    g.status = value >= min ? GateStatus::Pass : GateStatus::Fail;
    if (g.status == GateStatus::Fail) g.cause = std::move(failCause);
    return g;
}

inline GateResult gateAtMost(std::string id, std::int64_t value, std::int64_t max, std::string failCause)
{
    GateResult g;
    g.id = std::move(id);
    g.value = value;
    g.max = max;
    g.status = value <= max ? GateStatus::Pass : GateStatus::Fail;
    if (g.status == GateStatus::Fail) g.cause = std::move(failCause);
    return g;
}

inline GateResult gateAxiomSkipped()
{
    GateResult g;
    g.id = "axiom_directives_starved";
    g.status = GateStatus::Skipped;
    g.cause = "not implemented: axioms are not resolved into the forge yet";
    return g;
}

inline std::int64_t permilleOf(std::int64_t part, std::int64_t whole) { return whole == 0 ? 1000 : part * 1000 / whole; }

}

inline ValidationReport validateWorld(const ForgedWorld& w, const GateThresholds& t = {})
{
    ValidationReport report;
    report.seed = w.seed();
    report.worldHash = w.worldHash();
    if (!thresholdsValid(t)) {
        report.error = "gate thresholds are out of range";
        return report;
    }

    std::string unusable;
    if (!w.complete() || w.stopAfter() != ForgePhase::Content) {
        unusable = w.ok() ? "world was not forged through every phase" : "world did not complete: " + w.error();
    } else {
        unusable = detail::validationShapeError(w);
    }
    if (!unusable.empty()) {
        const auto unmet = [&](std::string id, std::int64_t min, std::int64_t max = kGateUnbounded) {
            GateResult g;
            g.id = std::move(id);
            g.min = min;
            g.max = max;
            g.cause = unusable;
            report.gates.push_back(std::move(g));
        };
        unmet("settlement_density", t.minSettlementsPerKm2Milli, t.maxSettlementsPerKm2Milli);
        unmet("road_connected", 0, t.maxRoadComponents);
        unmet("settlement_reach", t.minSettlementReachPermille);
        unmet("no_dead_ends", t.minPoiReachPermille);
        unmet("landmark_coverage", t.minLandmarkCoveragePermille);
        unmet("quest_items", t.minQuestItems);
        unmet("quest_cast", t.minQuestCast);
        unmet("land_share", t.minLandSharePermille, t.maxLandSharePermille);
        report.gates.back().value = kLandShareNotMeasured;
        report.gates.push_back(detail::gateAxiomSkipped());
        return report;
    }

    const SettlementMap& settlements = *w.settlements();
    const PoiMap& pois = *w.pois();
    const RoadMap& roads = *w.roads();
    const ContentManifest& manifest = *w.manifest();

    const std::int64_t areaM2 = static_cast<std::int64_t>(w.size()) * w.size() * t.cellSizeM * t.cellSizeM;
    const std::int64_t density = static_cast<std::int64_t>(settlements.sites.size()) * 1000000000 / areaM2;
    GateResult densityGate = detail::gateAtLeast("settlement_density", density, t.minSettlementsPerKm2Milli, "too few settlements per square km");
    densityGate.max = t.maxSettlementsPerKm2Milli;
    if (density > t.maxSettlementsPerKm2Milli) {
        densityGate.status = GateStatus::Fail;
        densityGate.cause = "too many settlements per square km";
    }
    report.gates.push_back(std::move(densityGate));

    report.gates.push_back(detail::gateAtMost("road_connected", roads.components, t.maxRoadComponents, "settlements split into separate road networks"));

    std::int64_t settlementReach = 1000;
    std::int64_t poiReach = pois.pois.empty() ? 1000 : 0;
    if (!settlements.sites.empty()) {
        const auto cellOf = [&](int x, int y) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(roads.width) + static_cast<std::size_t>(x); };
        const std::vector<std::uint32_t> label = detail::validationWalkComponents(w, t);
        std::vector<std::uint32_t> perLabel(label.size() + 1, 0);
        std::uint32_t best = 0;
        for (const Settlement& s : settlements.sites) {
            const std::uint32_t l = label[cellOf(s.x, s.y)];
            if (l != 0 && ++perLabel[l] > perLabel[best]) best = l;
        }
        const std::int64_t reachedSites = best == 0 ? 0 : perLabel[best];
        std::int64_t reachedPois = 0;
        if (best != 0) {
            for (const Poi& p : pois.pois) reachedPois += label[cellOf(p.x, p.y)] == best ? 1 : 0;
        }
        settlementReach = detail::permilleOf(reachedSites, static_cast<std::int64_t>(settlements.sites.size()));
        poiReach = detail::permilleOf(reachedPois, static_cast<std::int64_t>(pois.pois.size()));
    }
    report.gates.push_back(detail::gateAtLeast("settlement_reach", settlementReach, t.minSettlementReachPermille, "settlements are split across separate walkable areas"));
    report.gates.push_back(detail::gateAtLeast("no_dead_ends", poiReach, t.minPoiReachPermille, "a point of interest lies outside the main walkable area"));

    report.gates.push_back(
        detail::gateAtLeast("landmark_coverage", w.landmarkCoverage()->coveragePermille, t.minLandmarkCoveragePermille, "too much of the walkable world has no landmark in view"));
    report.gates.push_back(detail::gateAtLeast("quest_items", static_cast<std::int64_t>(manifest.items.size()), t.minQuestItems, "too few items placed at a ruin or dungeon"));
    report.gates.push_back(detail::gateAtLeast("quest_cast", static_cast<std::int64_t>(manifest.cast.size()), t.minQuestCast, "too few cast members bound to a settlement"));

    std::int64_t landCells = 0;
    for (const auto h : roads.heightM) landCells += h > t.seaLevelM ? 1 : 0;
    GateResult landGate = detail::gateAtLeast("land_share", landCells * 1000 / static_cast<std::int64_t>(roads.heightM.size()), t.minLandSharePermille, "too little of the world is above sea level");
    landGate.max = t.maxLandSharePermille;
    if (landGate.value > t.maxLandSharePermille) {
        landGate.status = GateStatus::Fail;
        landGate.cause = "too much of the world is above sea level";
    }
    report.gates.push_back(std::move(landGate));
    report.gates.push_back(detail::gateAxiomSkipped());
    return report;
}

struct ValidationAttempt {
    int index = 0;
    std::uint64_t seed = 0;
    ValidationReport report;
};

struct ValidatedForge {
    std::uint64_t worldSeed = 0;
    std::vector<ValidationAttempt> attempts;
    std::optional<ForgedWorld> world; // the accepted world, or the last attempt's when rejected
    bool accepted = false;
};

// attempt 0 is the world seed itself; attempt n >= 1 is rerollSeed(worldSeed, n).
constexpr std::uint64_t attemptSeed(std::uint64_t worldSeed, int attempt)
{
    return attempt <= 0 ? worldSeed : rerollSeed(worldSeed, static_cast<std::uint32_t>(attempt));
}

inline ValidatedForge forgeValidated(std::uint64_t worldSeed, int size = kForgeDefaultSize, const ForgeConfig& config = {}, const GateThresholds& thresholds = {},
    int maxAttempts = kValidateMaxAttempts)
{
    ValidatedForge out;
    out.worldSeed = worldSeed;
    const int attempts = std::clamp(maxAttempts, 1, kValidateMaxAttempts);
    for (int n = 0; n < attempts; ++n) {
        const std::uint64_t seed = attemptSeed(worldSeed, n);
        ForgedWorld world = forgeWorld(seed, size, config);
        ValidationReport report = validateWorld(world, thresholds);
        const bool passed = report.passed();
        const bool invalid = !report.error.empty();
        out.attempts.push_back({ n, seed, std::move(report) });
        out.world.emplace(std::move(world));
        if (passed) {
            out.accepted = true;
            break;
        }
        if (invalid) break;
    }
    return out;
}

namespace detail {

inline std::string validationJsonEscape(std::string_view s)
{
    std::string out;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u < 0x20) {
            static constexpr char kHex[] = "0123456789abcdef";
            out += "\\u00";
            out += kHex[u >> 4];
            out += kHex[u & 15];
        } else {
            out += c;
        }
    }
    return out;
}

inline std::string validationHex(std::uint64_t v)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4) s[static_cast<std::size_t>(i)] = kHex[v & 15];
    return s;
}

inline std::string validationBound(const GateResult& g)
{
    if (g.status == GateStatus::Skipped) return "-";
    const bool hasMax = g.max != kGateUnbounded;
    const bool hasMin = g.min != 0 || !hasMax;
    if (hasMin && hasMax) return std::to_string(g.min) + ".." + std::to_string(g.max);
    return hasMax ? "<= " + std::to_string(g.max) : ">= " + std::to_string(g.min);
}

inline std::string validationReportJson(const ValidationReport& r, const std::string& indent)
{
    std::string out = "{\"seed\": " + std::to_string(r.seed) + ", \"world_hash\": \"" + validationHex(r.worldHash) + "\", \"passed\": " + (r.passed() ? "true" : "false");
    if (!r.error.empty()) out += ", \"error\": \"" + validationJsonEscape(r.error) + "\"";
    out += ", \"gates\": [";
    for (std::size_t i = 0; i < r.gates.size(); ++i) {
        const GateResult& g = r.gates[i];
        out += (i ? ",\n" : "\n") + indent + "  {\"id\": \"" + validationJsonEscape(g.id) + "\", \"status\": \"" + std::string(gateStatusName(g.status)) + "\", \"value\": " + std::to_string(g.value);
        out += g.status == GateStatus::Skipped ? ", \"min\": null, \"max\": null" : ", \"min\": " + std::to_string(g.min) + ", \"max\": " + (g.max == kGateUnbounded ? "null" : std::to_string(g.max));
        out += ", \"cause\": \"" + validationJsonEscape(g.cause) + "\"}";
    }
    out += r.gates.empty() ? "]}" : "\n" + indent + "]}";
    return out;
}

}

// A pass/fail table: one row per gate.
inline std::string validationText(const ValidationReport& r)
{
    std::string out = "seed " + std::to_string(r.seed) + " world " + detail::validationHex(r.worldHash) + ": " + (r.passed() ? "PASS" : "FAIL") + "\n";
    if (!r.error.empty()) out += "  error: " + r.error + "\n";
    for (const GateResult& g : r.gates) {
        std::string id = g.id;
        id.resize(std::max<std::size_t>(id.size(), 26), ' ');
        std::string status(gateStatusName(g.status));
        status.resize(8, ' ');
        out += "  " + id + " " + status + " " + (g.status == GateStatus::Skipped ? std::string("-") : std::to_string(g.value)) + "  (" + detail::validationBound(g) + ")";
        if (!g.cause.empty()) out += "  " + g.cause;
        out += "\n";
    }
    return out;
}

inline std::string validationJson(const ValidationReport& r) { return detail::validationReportJson(r, "") + "\n"; }

// The file written next to the manifest as validation.json. Not part of the world hash.
inline std::string validatedForgeJson(const ValidatedForge& v)
{
    std::string out = "{\"world_seed\": " + std::to_string(v.worldSeed) + ", \"rejected\": " + (v.accepted ? "false" : "true") + ", \"attempts\": [";
    for (std::size_t i = 0; i < v.attempts.size(); ++i) {
        const ValidationAttempt& a = v.attempts[i];
        out += (i ? ",\n" : "\n") + std::string("  {\"attempt\": ") + std::to_string(a.index) + ", \"seed\": " + std::to_string(a.seed) + ", \"report\": " + detail::validationReportJson(a.report, "  ") + "}";
    }
    out += v.attempts.empty() ? "]}\n" : "\n]}\n";
    return out;
}

}
