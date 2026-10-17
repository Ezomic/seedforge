#pragma once

#include <worldforge/climate_map.h>
#include <worldforge/erosion_map.h>
#include <worldforge/hydrology_map.h>
#include <worldforge/resource_map.h>
#include <worldforge/subseed.h>
#include <worldforge/tectonics_map.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <vector>

// Phase 7, Settlements (doc 08): a suitability field (fresh water + arable + defensibility +
// resource proximity + coastal access, plus slope, temperature and an optional biome term), then
// greedy placement best-first with Poisson-disc spacing, and a size tier from the score. Phase 8
// (roads) and phase 9 (politics) consume SettlementMap; none of them may mutate it. The inputs
// are never modified.
//
// Axiom constraints arrive through SettlementParams (the axiom's "settlement" directive block):
//  - minElevationM:  hard constraint, no site below it (Deukalion's Flood: waterline + margin).
//  - preferGentleSlope: doubles the weight of the slope term (prefer_slope "gentle").
//  - densityMult256: scales the settlement count (density_mult 0.7 is 179), 256 = 1.0.
//  - traversalSpacingMult256: scales the Poisson-disc radius. Faster traversal (mounts, boats,
//    fast travel) lets the player cross more ground per minute, so sites spread out: use above 256.
//
// Output contract (SettlementMap, same grid as the inputs, row-major, index = y * width + x):
//  - suitability[c]: 0 for cells where a settlement can never stand (ocean, lake, river channel,
//                    below minElevationM), else 1 to 255 (best). A function of the inputs only,
//                    independent of the count and spacing parameters.
//  - sites:          at most min(maxSettlements * densityMult256 / 256, kSettlementMaxSites) sites
//                    in placement order (best first). Every site is a land cell with suitability
//                    >= minScore, and any two sites are at least the effective spacing apart
//                    (Euclidean, in cells). Fewer sites than the target, or none, is a valid
//                    result when the land cannot hold them.
//  - Settlement:     x, y cell; score = suitability of its cell; tier = SettlementTier from the
//                    score (monotonic: a higher score never gets a smaller tier); flags = bitset
//                    of kSettlementRiverside (a river or lake cell within 2 cells) and
//                    kSettlementCoastal (ocean within 2 cells).
//
// Optional input: biomes (phase 5, SEED-126) are not required. The biome term
// reads SettlementInputs::biomeSuitability, one byte per cell (0 hostile to 255 ideal). An empty
// span means "not available yet" and every cell gets kNeutralBiomeSuitability.
//
// Determinism: integer only, no libm, no floating point, no unordered container; ties resolve by
// a subseed.h hash then by cell index (the higher index wins an exact tie), so the result is
// bit-identical on every platform. Memory is O(cells), at most about 24 bytes per cell
// (transient), with cells <= kTectonicsMaxSize^2.
namespace worldforge {

enum class SettlementTier : std::uint8_t { Hamlet, Village, Town, City };

constexpr std::uint8_t kSettlementRiverside = 1;
constexpr std::uint8_t kSettlementCoastal = 2;
constexpr std::uint8_t kNeutralBiomeSuitability = 128;
constexpr int kSettlementMaxSites = 256;
constexpr int kSettlementMaxSpacing = 2048;
constexpr int kSettlementMaxDensityMult256 = 1024;
constexpr int kSettlementMinTraversalMult256 = 32;
constexpr int kSettlementMaxTraversalMult256 = 1024;
constexpr int kSettlementMaxEffectiveSpacing = 4096;

struct SettlementParams {
    int seaLevelM = 0;                      // cells at or below this are ocean, in the tectonics height range
    int minElevationM = kTectonicsMinHeightM; // hard floor for a site, in the tectonics height range
    int maxSettlements = 20;                // target count before density, in [0, kSettlementMaxSites]
    int minSpacing = 24;                    // Poisson-disc radius in cells, in [1, kSettlementMaxSpacing]
    int minScore = 48;                      // lowest suitability a site may have, in [1, 255]
    bool preferGentleSlope = false;
    int densityMult256 = 256;               // in [0, kSettlementMaxDensityMult256]
    int traversalSpacingMult256 = 256;      // in [kSettlementMinTraversalMult256, kSettlementMaxTraversalMult256]
};

struct SettlementInputs {
    const HydrologyMap* hydrology = nullptr;
    const ClimateMap* climate = nullptr;
    const ResourceMap* resources = nullptr;
    std::span<const std::uint8_t> biomeSuitability; // empty, or width * height bytes
};

struct Settlement {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint8_t score = 0;
    std::uint8_t tier = 0;
    std::uint8_t flags = 0;
};

struct SettlementMap {
    std::uint64_t seed = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> suitability;
    std::vector<Settlement> sites;

    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); }
};

namespace detail {

constexpr std::uint64_t kSettlementDomain = 0x7365747470686137ull;
constexpr std::uint32_t kSettlementSaltTie = 1;

constexpr std::uint16_t kSettlementUnreached = 0xFFFF;
constexpr int kSettlementWaterReach = 10;
constexpr int kSettlementCoastReach = 8;
constexpr int kSettlementOreReach = 16;
constexpr int kSettlementSlopeCapM = 40;
constexpr int kSettlementProminenceStep = 3;
constexpr int kSettlementProminenceCapM = 30;
constexpr int kSettlementNearFlagReach = 2;
constexpr int kSettlementTierVillage = 100;
constexpr int kSettlementTierTown = 140;
constexpr int kSettlementTierCity = 180;

inline bool settlementInputValid(const SettlementInputs& in)
{
    if (in.hydrology == nullptr || in.climate == nullptr || in.resources == nullptr) return false;
    const HydrologyMap& h = *in.hydrology;
    const ClimateMap& c = *in.climate;
    const ResourceMap& r = *in.resources;
    if (h.width < kTectonicsMinSize || h.width > kTectonicsMaxSize || h.height < kTectonicsMinSize || h.height > kTectonicsMaxSize) return false;
    if (c.width != h.width || c.height != h.height || r.width != h.width || r.height != h.height) return false;
    if (c.seed != h.seed || r.seed != h.seed) return false;
    const std::size_t cells = static_cast<std::size_t>(h.width) * static_cast<std::size_t>(h.height);
    if (h.heightM.size() != cells || h.lakeDepthM.size() != cells || h.river.size() != cells) return false;
    if (c.temperatureC10.size() != cells || r.arable.size() != cells || r.oreGrade.size() != cells) return false;
    if (!in.biomeSuitability.empty() && in.biomeSuitability.size() != cells) return false;
    return std::all_of(h.heightM.begin(), h.heightM.end(), [](std::int16_t v) { return v >= kTectonicsMinHeightM && v <= kTectonicsMaxHeightM; });
}

inline bool settlementParamsValid(const SettlementParams& p)
{
    return p.seaLevelM >= kTectonicsMinHeightM && p.seaLevelM <= kTectonicsMaxHeightM && p.minElevationM >= kTectonicsMinHeightM
        && p.minElevationM <= kTectonicsMaxHeightM && p.maxSettlements >= 0 && p.maxSettlements <= kSettlementMaxSites && p.minSpacing >= 1
        && p.minSpacing <= kSettlementMaxSpacing && p.minScore >= 1 && p.minScore <= 255 && p.densityMult256 >= 0
        && p.densityMult256 <= kSettlementMaxDensityMult256 && p.traversalSpacingMult256 >= kSettlementMinTraversalMult256
        && p.traversalSpacingMult256 <= kSettlementMaxTraversalMult256;
}

// 4-neighbour breadth-first distance from the cells where `source` is 1, capped below `reach`.
inline std::vector<std::uint16_t> settlementDistance(int w, int h, const std::vector<std::uint8_t>& source, int reach)
{
    const std::size_t cells = source.size();
    std::vector<std::uint16_t> dist(cells, kSettlementUnreached);
    std::vector<std::uint32_t> queue;
    queue.reserve(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        if (source[c] != 0) {
            dist[c] = 0;
            queue.push_back(static_cast<std::uint32_t>(c));
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::uint32_t c = queue[head];
        const int d = dist[c];
        if (d + 1 >= reach) continue;
        const int x = static_cast<int>(c % static_cast<std::uint32_t>(w));
        const int y = static_cast<int>(c / static_cast<std::uint32_t>(w));
        for (int n = 0; n < 4; ++n) {
            const int nx = x + (n == 0 ? 1 : n == 1 ? -1 : 0);
            const int ny = y + (n == 2 ? 1 : n == 3 ? -1 : 0);
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const std::size_t nc = static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx);
            if (dist[nc] != kSettlementUnreached) continue;
            dist[nc] = static_cast<std::uint16_t>(d + 1);
            queue.push_back(static_cast<std::uint32_t>(nc));
        }
    }
    return dist;
}

// 255 at distance 0, falling linearly to 0 at `reach`.
inline std::int64_t settlementProximity(std::uint16_t d, int reach)
{
    if (d == kSettlementUnreached || d >= reach) return 0;
    return static_cast<std::int64_t>(reach - d) * 255 / reach;
}

inline std::uint8_t settlementTier(int score)
{
    if (score >= kSettlementTierCity) return static_cast<std::uint8_t>(SettlementTier::City);
    if (score >= kSettlementTierTown) return static_cast<std::uint8_t>(SettlementTier::Town);
    if (score >= kSettlementTierVillage) return static_cast<std::uint8_t>(SettlementTier::Village);
    return static_cast<std::uint8_t>(SettlementTier::Hamlet);
}

inline std::int64_t settlementTemperatureScore(int tempC10)
{
    constexpr int kLow = -150;
    constexpr int kIdealLow = 100;
    constexpr int kIdealHigh = 250;
    constexpr int kHigh = 450;
    if (tempC10 <= kLow || tempC10 >= kHigh) return 0;
    if (tempC10 < kIdealLow) return static_cast<std::int64_t>(tempC10 - kLow) * 255 / (kIdealLow - kLow);
    if (tempC10 > kIdealHigh) return static_cast<std::int64_t>(kHigh - tempC10) * 255 / (kHigh - kIdealHigh);
    return 255;
}

inline std::vector<std::uint8_t> scoreSettlements(const SettlementInputs& in, const SettlementParams& p)
{
    const HydrologyMap& hy = *in.hydrology;
    const int w = hy.width;
    const int h = hy.height;
    const std::size_t cells = hy.heightM.size();
    const std::vector<std::int16_t>& z = hy.heightM;

    std::vector<std::uint8_t> freshWater(cells, 0);
    std::vector<std::uint8_t> ocean(cells, 0);
    std::vector<std::uint8_t> ore(cells, 0);
    for (std::size_t c = 0; c < cells; ++c) {
        ocean[c] = z[c] <= p.seaLevelM ? 1 : 0;
        freshWater[c] = (hy.river[c] != 0 || hy.lakeDepthM[c] != 0) && ocean[c] == 0 ? 1 : 0;
        ore[c] = in.resources->oreGrade[c] != 0 ? 1 : 0;
    }
    const std::vector<std::uint16_t> waterDist = settlementDistance(w, h, freshWater, kSettlementWaterReach);
    const std::vector<std::uint16_t> coastDist = settlementDistance(w, h, ocean, kSettlementCoastReach);
    const std::vector<std::uint16_t> oreDist = settlementDistance(w, h, ore, kSettlementOreReach);

    constexpr std::int64_t kWaterW = 30;
    constexpr std::int64_t kArableW = 25;
    constexpr std::int64_t kDefenseW = 5;
    constexpr std::int64_t kResourceW = 5;
    constexpr std::int64_t kCoastW = 10;
    constexpr std::int64_t kTempW = 5;
    constexpr std::int64_t kBiomeW = 5;
    const std::int64_t slopeW = p.preferGentleSlope ? 30 : 15;
    const std::int64_t totalW = kWaterW + kArableW + kDefenseW + kResourceW + kCoastW + kTempW + kBiomeW + slopeW;

    const auto heightAt = [&](int x, int y) { return static_cast<int>(z[static_cast<std::size_t>(std::clamp(y, 0, h - 1)) * static_cast<std::size_t>(w) + static_cast<std::size_t>(std::clamp(x, 0, w - 1))]); };

    std::vector<std::uint8_t> out(cells, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t c = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const int elev = z[c];
            if (ocean[c] != 0 || hy.lakeDepthM[c] != 0 || hy.river[c] != 0 || elev < p.minElevationM) continue;

            int slope = 0;
            slope = std::max(slope, std::abs(elev - heightAt(x + 1, y)));
            slope = std::max(slope, std::abs(elev - heightAt(x - 1, y)));
            slope = std::max(slope, std::abs(elev - heightAt(x, y + 1)));
            slope = std::max(slope, std::abs(elev - heightAt(x, y - 1)));
            const std::int64_t slopeScore = std::max(0, 255 - slope * 255 / kSettlementSlopeCapM);

            const int s = kSettlementProminenceStep;
            const int ringMean = (heightAt(x + s, y) + heightAt(x - s, y) + heightAt(x, y + s) + heightAt(x, y - s)) / 4;
            const std::int64_t defenseScore = std::clamp(elev - ringMean, 0, kSettlementProminenceCapM) * 255 / kSettlementProminenceCapM;

            const std::int64_t biome = in.biomeSuitability.empty() ? kNeutralBiomeSuitability : in.biomeSuitability[c];
            const std::int64_t sum = kWaterW * settlementProximity(waterDist[c], kSettlementWaterReach) + kArableW * in.resources->arable[c]
                + kDefenseW * defenseScore + kResourceW * settlementProximity(oreDist[c], kSettlementOreReach)
                + kCoastW * settlementProximity(coastDist[c], kSettlementCoastReach) + kTempW * settlementTemperatureScore(in.climate->temperatureC10[c])
                + kBiomeW * biome + slopeW * slopeScore;
            out[c] = static_cast<std::uint8_t>(std::clamp<std::int64_t>(sum / totalW, 1, 255));
        }
    }
    return out;
}

inline std::uint8_t settlementFlags(const SettlementInputs& in, const SettlementParams& p, int x, int y)
{
    const HydrologyMap& hy = *in.hydrology;
    std::uint8_t flags = 0;
    constexpr int r = kSettlementNearFlagReach;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= hy.width || ny >= hy.height) continue;
            const std::size_t n = hy.index(nx, ny);
            if (hy.heightM[n] <= p.seaLevelM) flags |= kSettlementCoastal;
            else if (hy.river[n] != 0 || hy.lakeDepthM[n] != 0) flags |= kSettlementRiverside;
        }
    }
    return flags;
}

inline void placeSettlements(const SettlementInputs& in, const SettlementParams& p, SettlementMap& out)
{
    const int target = static_cast<int>(std::min<std::int64_t>(kSettlementMaxSites, static_cast<std::int64_t>(p.maxSettlements) * p.densityMult256 / 256));
    if (target == 0) return;
    const int spacing = static_cast<int>(std::min<std::int64_t>(kSettlementMaxEffectiveSpacing, static_cast<std::int64_t>(p.minSpacing) * p.traversalSpacingMult256 / 256));
    const std::int64_t spacingSq = static_cast<std::int64_t>(spacing) * spacing;

    std::vector<std::uint64_t> keys;
    for (std::size_t c = 0; c < out.suitability.size(); ++c) {
        if (out.suitability[c] < p.minScore) continue;
        const std::uint64_t tie = splitmix64(splitmix64(out.seed ^ kSettlementDomain) ^ hashCell(static_cast<std::int32_t>(kSettlementSaltTie), static_cast<std::int32_t>(c))) & 0xFFFFu;
        keys.push_back((static_cast<std::uint64_t>(out.suitability[c]) << 48) | (tie << 32) | static_cast<std::uint64_t>(c));
    }
    std::sort(keys.begin(), keys.end(), [](std::uint64_t a, std::uint64_t b) { return a > b; });

    const int bw = (out.width + spacing - 1) / spacing;
    const int bh = (out.height + spacing - 1) / spacing;
    std::vector<std::int32_t> head(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh), -1);
    std::vector<std::int32_t> next;

    for (const std::uint64_t key : keys) {
        if (static_cast<int>(out.sites.size()) >= target) break;
        const std::int64_t c = static_cast<std::int64_t>(key & 0xFFFFFFFFu);
        const int x = static_cast<int>(c % out.width);
        const int y = static_cast<int>(c / out.width);
        const int bx = x / spacing;
        const int by = y / spacing;
        bool clear = true;
        for (int ny = std::max(0, by - 1); ny <= std::min(bh - 1, by + 1) && clear; ++ny) {
            for (int nx = std::max(0, bx - 1); nx <= std::min(bw - 1, bx + 1) && clear; ++nx) {
                for (std::int32_t s = head[static_cast<std::size_t>(ny) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(nx)]; s >= 0; s = next[static_cast<std::size_t>(s)]) {
                    const std::int64_t dx = out.sites[static_cast<std::size_t>(s)].x - x;
                    const std::int64_t dy = out.sites[static_cast<std::size_t>(s)].y - y;
                    if (dx * dx + dy * dy < spacingSq) {
                        clear = false;
                        break;
                    }
                }
            }
        }
        if (!clear) continue;
        const std::size_t bucket = static_cast<std::size_t>(by) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(bx);
        next.push_back(head[bucket]);
        head[bucket] = static_cast<std::int32_t>(out.sites.size());
        const int score = out.suitability[static_cast<std::size_t>(c)];
        out.sites.push_back({ static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::uint8_t>(score), settlementTier(score), settlementFlags(in, p, x, y) });
    }
}

}

// Returns nullopt when an input is missing or malformed (null maps, size outside the kTectonics*
// limits, vectors of the wrong length, heights out of range, maps from another seed or grid, a
// biome span that is neither empty nor width * height long), or a parameter is out of range.
// Otherwise a pure function of (hydrology, climate, resources, biomeSuitability, params).
inline std::optional<SettlementMap> generateSettlements(const SettlementInputs& inputs, const SettlementParams& params = {})
{
    if (!detail::settlementInputValid(inputs) || !detail::settlementParamsValid(params)) return std::nullopt;
    SettlementMap out;
    out.seed = inputs.hydrology->seed;
    out.width = inputs.hydrology->width;
    out.height = inputs.hydrology->height;
    out.suitability = detail::scoreSettlements(inputs, params);
    detail::placeSettlements(inputs, params, out);
    return out;
}

// Order-sensitive digest of every field of the map; the golden value that guards determinism.
inline std::uint64_t hashSettlementMap(const SettlementMap& map)
{
    std::uint64_t h = splitmix64(map.seed ^ detail::kSettlementDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    for (const std::uint8_t s : map.suitability) mix(s);
    mix(map.sites.size());
    for (const Settlement& s : map.sites) {
        mix((static_cast<std::uint64_t>(static_cast<std::uint16_t>(s.x)) << 32) | (static_cast<std::uint64_t>(static_cast<std::uint16_t>(s.y)) << 16)
            | (static_cast<std::uint64_t>(s.score) << 8) | (static_cast<std::uint64_t>(s.tier) << 4) | s.flags);
    }
    return h;
}

}
