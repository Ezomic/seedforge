#pragma once

#include <worldforge/landmark_coverage.h>
#include <worldforge/road_map.h>
#include <worldforge/settlement_map.h>
#include <worldforge/subseed.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

// Phase 10, POI placement with sightline scoring (doc 11, "Navigation: landmarks over waypoints"):
// an entrance nobody can see may as well not exist, so points of interest are put where travellers
// on the roads can actually see them. The RoadMap and the SettlementMap are only read.
//
// Inputs:
//  - RoadMap: heightM (the deformed terrain the roads run on) is the terrain the sight lines
//    cross; road marks the road cells; pathCells (the route of each edge, in edge order) orders
//    the viewpoints.
//  - SettlementMap (optional, nullptr = none): candidates keep settlementClearance cells away
//    from every site. Must share the RoadMap grid and seed.
//
// Viewpoints: the distinct cells of pathCells, in first-visit order, thinned by a stride k (the
// smallest value that leaves at most maxViewpoints). Every k-th distinct cell is a viewpoint, so
// they follow the road splines evenly. A viewpoint sees with its eye eyeHeightM above the ground.
//
// Candidates: land cells (height above seaLevelM) that are not road cells, with no road cell
// within roadClearance (Chebyshev) and no settlement within settlementClearance (Chebyshev), on a
// regular grid whose stride is the smallest value that keeps it at or below maxCandidates points.
//
// Sightline score of a candidate, in permille [0, 1000]: for each viewpoint strictly within viewRangeCells
// (Euclidean) whose integer line of sight (landmark_coverage.h, same ray convention, the ray runs
// from the eye to the entrance top) clears the terrain, a weight of 256 * (range^2 - d^2) / range^2
// (nearer sees better); the score is the weight sum * 1000 / (viewpoints * 256), rounded down. The
// entrance top is entranceHeightM above the ground at the candidate.
//
// Placement: candidates whose score is at least minScorePermille are taken best first (ties by a
// seed-derived hash, then cell), skipping any within minSpacing cells (Euclidean) of one already
// taken, up to maxPois - coverageReserve. Zero viewpoints (no roads) scores nothing, so only the
// coverage pass can place.
//
// Coverage pass (coverageReserve > 0): up to coverageReserve more POIs, each the clear candidate (a
// land cell with the road and settlement clearance, on a grid of at most 1024 cells) that covers the
// most samples not yet covered under the landmark validator's own rule and sample grid
// (landmark_coverage.h, params.coverage and coverageHeightM), so a world is not left with land no
// landmark can be seen from. The score-placed POIs count as already placed, but only the first
// kLandmarkMaxLandmarks - coverageReserve of them, because the validator reads only the first
// kLandmarkMaxLandmarks POIs. The coverage POIs are inserted right after those, ahead of any later
// score-placed POI, so they are inside the validator's window. Ties by the seed-derived hash, then
// cell. The pass stops early when no candidate covers a new sample.
//
// Output (PoiMap): the grid, the viewpoint count and the placed POIs in placement order (best
// first), each with its cell, score, and the number of viewpoints that see it.
//
// Determinism: integer only, no libm, no floating point, no unordered container; every sort key
// ends in the cell. Memory is O(cells) for the seen bitmap during viewpoint collection plus
// O(viewpoints + candidates). Work is at most maxCandidates * maxViewpoints * viewRangeCells
// line-of-sight steps (4096 * 512 * 128, about 268 million, in practice far fewer because
// viewpoints beyond range are skipped by a distance test).
namespace worldforge {

constexpr int kPoiMaxPois = 256;
constexpr int kPoiMaxSpacing = 2048;
constexpr int kPoiMaxViewRangeCells = 128;
constexpr int kPoiMaxEyeM = kLandmarkMaxEyeM;
constexpr int kPoiMaxEntranceM = 64;
constexpr int kPoiMaxCandidates = 4096;
constexpr int kPoiMaxViewpoints = 512;
constexpr int kPoiMaxClearance = 64;

struct PoiParams {
    int seaLevelM = 0;           // cells at or below this are not land, in the tectonics height range
    int maxPois = 16;            // in [0, kPoiMaxPois]
    int minSpacing = 24;         // between POIs, cells, in [1, kPoiMaxSpacing]
    int minScorePermille = 20;   // lowest sightline score a POI may have, in [0, 1000]
    int viewRangeCells = 64;     // farthest a traveller sees an entrance, in [1, kPoiMaxViewRangeCells]
    int eyeHeightM = 2;          // viewpoint eye above ground, in [0, kPoiMaxEyeM]
    int entranceHeightM = 4;     // top of the visible entrance above ground, in [1, kPoiMaxEntranceM]
    int maxCandidates = 2048;    // candidate grid size cap, in [1, kPoiMaxCandidates]
    int maxViewpoints = 256;     // road viewpoint cap, in [1, kPoiMaxViewpoints]
    int roadClearance = 2;       // no road within this many cells of a POI, in [0, kPoiMaxClearance]
    int settlementClearance = 6; // no settlement within this many cells of a POI, in [0, kPoiMaxClearance]
    int coverageReserve = 0;     // of maxPois, how many are placed to widen landmark coverage instead of by score, in [0, maxPois]
    int coverageHeightM = 24;    // silhouette height the coverage pass gives a POI, in [1, kLandmarkMaxHeightM]
    LandmarkCoverageParams coverage; // sampling, sight and near ranges the coverage pass optimises, same meaning as the validator's
};

struct Poi {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint16_t score = 0;        // sightline score, permille
    std::uint16_t visibleCount = 0; // viewpoints with a line of sight to the entrance
};

struct PoiMap {
    std::uint64_t seed = 0; // RoadMap::seed it was derived from
    int width = 0;
    int height = 0;
    int viewpointCount = 0;
    std::vector<Poi> pois;
};

struct PoiViewpoint {
    int x = 0;
    int y = 0;
};

struct PoiSightline {
    int scorePermille = 0;
    int visibleCount = 0;
};

namespace detail {

constexpr std::uint64_t kPoiDomain = 0x706f695f70683130ull;
constexpr std::uint64_t kPoiSaltTie = 1;

inline bool poiInputValid(const RoadMap& roads, const SettlementMap* settlements)
{
    if (roads.width < kTectonicsMinSize || roads.width > kTectonicsMaxSize || roads.height < kTectonicsMinSize || roads.height > kTectonicsMaxSize) return false;
    const std::size_t cells = static_cast<std::size_t>(roads.width) * static_cast<std::size_t>(roads.height);
    if (roads.heightM.size() != cells || roads.road.size() != cells) return false;
    if (!std::all_of(roads.heightM.begin(), roads.heightM.end(), [](std::int16_t v) { return v >= kTectonicsMinHeightM && v <= kTectonicsMaxHeightM; })) return false;
    if (roads.pathCells.size() > kRoadsMaxPathCells) return false;
    if (!std::all_of(roads.pathCells.begin(), roads.pathCells.end(), [&](std::uint32_t c) { return c < cells; })) return false;
    if (settlements == nullptr) return true;
    if (settlements->width != roads.width || settlements->height != roads.height || settlements->seed != roads.seed) return false;
    if (settlements->sites.size() > static_cast<std::size_t>(kSettlementMaxSites)) return false;
    return std::all_of(settlements->sites.begin(), settlements->sites.end(), [&](const Settlement& s) { return s.x >= 0 && s.x < roads.width && s.y >= 0 && s.y < roads.height; });
}

inline bool poiParamsValid(const PoiParams& p)
{
    return p.seaLevelM >= kTectonicsMinHeightM && p.seaLevelM <= kTectonicsMaxHeightM && p.maxPois >= 0 && p.maxPois <= kPoiMaxPois && p.minSpacing >= 1
        && p.minSpacing <= kPoiMaxSpacing && p.minScorePermille >= 0 && p.minScorePermille <= 1000 && p.viewRangeCells >= 1 && p.viewRangeCells <= kPoiMaxViewRangeCells
        && p.eyeHeightM >= 0 && p.eyeHeightM <= kPoiMaxEyeM && p.entranceHeightM >= 1 && p.entranceHeightM <= kPoiMaxEntranceM && p.maxCandidates >= 1
        && p.maxCandidates <= kPoiMaxCandidates && p.maxViewpoints >= 1 && p.maxViewpoints <= kPoiMaxViewpoints && p.roadClearance >= 0
        && p.roadClearance <= kPoiMaxClearance && p.settlementClearance >= 0 && p.settlementClearance <= kPoiMaxClearance && p.coverageReserve >= 0
        && p.coverageReserve <= p.maxPois && p.coverageReserve <= kLandmarkMaxLandmarks
        && (p.coverageReserve == 0 || (p.coverageHeightM >= 1 && p.coverageHeightM <= kLandmarkMaxHeightM && landmarkParamsValid(p.coverage)));
}

struct PoiCandidate {
    int x = 0;
    int y = 0;
    int score = 0;
    int visible = 0;
    std::uint64_t tie = 0;
};

inline bool poiClear(const RoadMap& roads, const SettlementMap* settlements, const PoiParams& p, int x, int y)
{
    if (roads.heightM[roads.index(x, y)] <= p.seaLevelM) return false;
    const int r = p.roadClearance;
    for (int yy = std::max(0, y - r); yy <= std::min(roads.height - 1, y + r); ++yy) {
        for (int xx = std::max(0, x - r); xx <= std::min(roads.width - 1, x + r); ++xx) {
            if (roads.road[roads.index(xx, yy)] != 0) return false;
        }
    }
    if (settlements == nullptr) return true;
    const int s = p.settlementClearance;
    return std::none_of(settlements->sites.begin(), settlements->sites.end(), [&](const Settlement& site) { return std::abs(site.x - x) <= s && std::abs(site.y - y) <= s; });
}

struct PoiCoverageSample {
    int x = 0;
    int y = 0;
    std::int64_t ground = 0;
};

// True when a landmark at (lx, ly) covers the sample under the landmark validator's rule: inside the near range, or inside the view range with a clear line of sight.
inline bool poiCovers(const LandmarkCoverageInputs& terrain, const LandmarkCoverageParams& cov, int heightM, const PoiCoverageSample& s, int lx, int ly)
{
    const std::int64_t dx = lx - s.x;
    const std::int64_t dy = ly - s.y;
    const std::int64_t dSq = dx * dx + dy * dy;
    if (dSq <= static_cast<std::int64_t>(cov.nearRangeCells) * cov.nearRangeCells) return true;
    if (dSq > static_cast<std::int64_t>(cov.viewRangeCells) * cov.viewRangeCells) return false;
    return landmarkLineOfSight(terrain, s.x, s.y, s.ground + cov.eyeHeightM, Landmark { lx, ly, heightM });
}

// Greedy landmark coverage: up to `count` extra POIs, each the clear candidate cell that covers the most still uncovered samples of the
// validator's own sample grid (ties by the seed hash, then cell). `placed` is every POI already taken (spacing), of which the first
// `counted` are the landmarks the validator will see. Stops early when no candidate covers a new sample.
inline std::vector<Poi> placeCoveragePois(const RoadMap& roads, const SettlementMap* settlements, const PoiParams& p, const std::vector<Poi>& placed, std::size_t counted,
    int count, std::uint64_t tieSeed)
{
    constexpr int kMaxCoverageCandidates = 1024;
    const LandmarkCoverageInputs terrain { roads.width, roads.height, roads.heightM, {} };
    const LandmarkCoverageParams& cov = p.coverage;

    std::vector<PoiCoverageSample> samples;
    const int sampleStride = landmarkStride(roads.width, roads.height, cov.maxSamples);
    for (int y = sampleStride / 2; y < roads.height; y += sampleStride) {
        for (int x = sampleStride / 2; x < roads.width; x += sampleStride) {
            const std::int64_t ground = roads.heightM[roads.index(x, y)];
            if (ground > cov.seaLevelM) samples.push_back({ x, y, ground });
        }
    }
    std::vector<Poi> out;
    if (samples.empty() || count <= 0) return out;

    const std::size_t words = (samples.size() + 63) / 64;
    std::vector<std::uint64_t> covered(words, 0);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        for (std::size_t k = 0; k < std::min(counted, placed.size()); ++k) {
            if (poiCovers(terrain, cov, p.coverageHeightM, samples[i], placed[k].x, placed[k].y)) {
                covered[i / 64] |= std::uint64_t { 1 } << (i % 64);
                break;
            }
        }
    }

    struct Candidate {
        int x = 0;
        int y = 0;
        std::uint64_t tie = 0;
        std::size_t row = 0;
    };
    const std::int64_t spacingSq = static_cast<std::int64_t>(p.minSpacing) * p.minSpacing;
    std::vector<Poi> taken = placed;
    const int baseStride = [&] {
        int stride = 1;
        while (static_cast<std::int64_t>((roads.width + stride - 1) / stride) * ((roads.height + stride - 1) / stride) > kMaxCoverageCandidates) ++stride;
        return stride;
    }();

    // Strict clearances first; crowded land (thin continents full of settlements) has no clear cell, so the clearances relax in two steps.
    for (int level = 0; level < 3 && static_cast<int>(out.size()) < count; ++level) {
        const bool allCovered = std::all_of(samples.begin(), samples.end(), [&, i = std::size_t { 0 }](const PoiCoverageSample&) mutable {
            const bool bit = (covered[i / 64] >> (i % 64)) & 1;
            ++i;
            return bit;
        });
        if (allCovered) break;
        PoiParams q = p;
        q.roadClearance = p.roadClearance >> level;
        q.settlementClearance = p.settlementClearance >> level;
        if (level == 2) {
            q.roadClearance = 0;
            q.settlementClearance = std::min(p.settlementClearance, 1);
        }

        std::vector<Candidate> cands;
        std::vector<std::uint64_t> rows;
        for (int stride = baseStride;; stride = (stride + 1) / 2) {
            for (int y = stride / 2; y < roads.height && static_cast<int>(cands.size()) < kMaxCoverageCandidates; y += stride) {
                for (int x = stride / 2; x < roads.width && static_cast<int>(cands.size()) < kMaxCoverageCandidates; x += stride) {
                    if (!poiClear(roads, settlements, q, x, y)) continue;
                    const std::size_t row = rows.size();
                    rows.resize(row + words, 0);
                    bool any = false;
                    for (std::size_t i = 0; i < samples.size(); ++i) {
                        if (!poiCovers(terrain, cov, p.coverageHeightM, samples[i], x, y)) continue;
                        rows[row + i / 64] |= std::uint64_t { 1 } << (i % 64);
                        any = true;
                    }
                    if (!any) {
                        rows.resize(row);
                        continue;
                    }
                    cands.push_back({ x, y, splitmix64(tieSeed ^ hashCell(x, y)), row });
                }
            }
            if (!cands.empty() || stride == 1) break;
        }

        while (static_cast<int>(out.size()) < count) {
            const Candidate* best = nullptr;
            int bestGain = 0;
            for (const Candidate& c : cands) {
                int gain = 0;
                for (std::size_t w = 0; w < words; ++w) gain += std::popcount(rows[c.row + w] & ~covered[w]);
                if (gain == 0 || gain < bestGain) continue;
                if (gain == bestGain && best != nullptr && (c.tie > best->tie || (c.tie == best->tie && (c.y > best->y || (c.y == best->y && c.x > best->x))))) continue;
                const bool crowded = std::any_of(taken.begin(), taken.end(), [&](const Poi& t) {
                    const std::int64_t dx = t.x - c.x;
                    const std::int64_t dy = t.y - c.y;
                    return dx * dx + dy * dy < spacingSq;
                });
                if (crowded) continue;
                best = &c;
                bestGain = gain;
            }
            if (best == nullptr) break;
            for (std::size_t w = 0; w < words; ++w) covered[w] |= rows[best->row + w];
            const Poi poi { static_cast<std::int16_t>(best->x), static_cast<std::int16_t>(best->y), 0, 0 };
            out.push_back(poi);
            taken.push_back(poi);
        }
    }
    return out;
}

}

// The viewpoints along the road splines; see the header comment. Exposed so callers and tests can
// score arbitrary cells against the same set the placement used. The RoadMap must be valid.
inline std::vector<PoiViewpoint> poiViewpoints(const RoadMap& roads, int maxViewpoints)
{
    std::vector<PoiViewpoint> out;
    std::vector<std::uint8_t> seen(roads.road.size(), 0);
    std::vector<std::uint32_t> distinct;
    for (const std::uint32_t c : roads.pathCells) {
        if (seen[c]) continue;
        seen[c] = 1;
        distinct.push_back(c);
    }
    const std::size_t cap = static_cast<std::size_t>(std::max(maxViewpoints, 1));
    const std::size_t k = std::max<std::size_t>((distinct.size() + cap - 1) / cap, 1);
    for (std::size_t i = 0; i < distinct.size(); i += k) {
        out.push_back({ static_cast<int>(distinct[i] % static_cast<std::uint32_t>(roads.width)), static_cast<int>(distinct[i] / static_cast<std::uint32_t>(roads.width)) });
    }
    return out;
}

// Sightline score for an entrance at (x, y). The caller guarantees the cell is on the map and the
// parameters are valid.
inline PoiSightline poiSightline(const RoadMap& roads, const std::vector<PoiViewpoint>& viewpoints, const PoiParams& p, int x, int y)
{
    if (viewpoints.empty()) return {};
    const LandmarkCoverageInputs terrain { roads.width, roads.height, roads.heightM, {} };
    const Landmark entrance { x, y, p.entranceHeightM };
    const std::int64_t rangeSq = static_cast<std::int64_t>(p.viewRangeCells) * p.viewRangeCells;
    std::int64_t weight = 0;
    int visible = 0;
    for (const PoiViewpoint& v : viewpoints) {
        const std::int64_t dx = v.x - x;
        const std::int64_t dy = v.y - y;
        const std::int64_t dSq = dx * dx + dy * dy;
        if (dSq >= rangeSq) continue;
        const std::int64_t eye = static_cast<std::int64_t>(roads.heightM[roads.index(v.x, v.y)]) + p.eyeHeightM;
        if (!detail::landmarkLineOfSight(terrain, v.x, v.y, eye, entrance)) continue;
        weight += 256 * (rangeSq - dSq) / rangeSq;
        ++visible;
    }
    const std::int64_t score = weight * 1000 / (static_cast<std::int64_t>(viewpoints.size()) * 256);
    return { static_cast<int>(score), visible };
}

// Returns nullopt when the input is malformed (a grid outside the kTectonics* limits, vectors of
// the wrong length, heights out of range, a path cell off the map, a SettlementMap from another
// grid or seed, with a site off the map or more than kSettlementMaxSites sites) or a parameter is out of range. Otherwise a pure
// function of (roads, settlements, params).
inline std::optional<PoiMap> placePois(const RoadMap& roads, const SettlementMap* settlements, const PoiParams& params = {})
{
    if (!detail::poiInputValid(roads, settlements) || !detail::poiParamsValid(params)) return std::nullopt;

    PoiMap out;
    out.seed = roads.seed;
    out.width = roads.width;
    out.height = roads.height;
    const std::vector<PoiViewpoint> viewpoints = poiViewpoints(roads, params.maxViewpoints);
    out.viewpointCount = static_cast<int>(viewpoints.size());
    if (params.maxPois == 0) return out;
    const int scoreBudget = params.maxPois - params.coverageReserve;

    int stride = 1;
    while (static_cast<std::int64_t>((roads.width + stride - 1) / stride) * ((roads.height + stride - 1) / stride) > params.maxCandidates) ++stride;

    const std::uint64_t tieSeed = splitmix64(roads.seed ^ detail::kPoiDomain) ^ detail::kPoiSaltTie;
    std::vector<detail::PoiCandidate> cands;
    for (int y = stride / 2; y < roads.height && !viewpoints.empty(); y += stride) {
        for (int x = stride / 2; x < roads.width; x += stride) {
            if (!detail::poiClear(roads, settlements, params, x, y)) continue;
            const PoiSightline s = poiSightline(roads, viewpoints, params, x, y);
            if (s.scorePermille < params.minScorePermille) continue;
            cands.push_back({ x, y, s.scorePermille, s.visibleCount, splitmix64(tieSeed ^ hashCell(x, y)) });
        }
    }
    std::sort(cands.begin(), cands.end(), [](const detail::PoiCandidate& a, const detail::PoiCandidate& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.tie != b.tie) return a.tie < b.tie;
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });

    const std::int64_t spacingSq = static_cast<std::int64_t>(params.minSpacing) * params.minSpacing;
    for (const detail::PoiCandidate& c : cands) {
        if (static_cast<int>(out.pois.size()) >= params.maxPois) break;
        const bool crowded = std::any_of(out.pois.begin(), out.pois.end(), [&](const Poi& q) {
            const std::int64_t dx = q.x - c.x;
            const std::int64_t dy = q.y - c.y;
            return dx * dx + dy * dy < spacingSq;
        });
        if (crowded) continue;
        out.pois.push_back({ static_cast<std::int16_t>(c.x), static_cast<std::int16_t>(c.y), static_cast<std::uint16_t>(c.score), static_cast<std::uint16_t>(c.visible) });
    }
    if (params.coverageReserve == 0) return out;

    const std::size_t scored = std::min<std::size_t>(out.pois.size(), static_cast<std::size_t>(scoreBudget));
    const std::size_t counted = std::min<std::size_t>(scored, static_cast<std::size_t>(kLandmarkMaxLandmarks - params.coverageReserve));
    const std::vector<Poi> scoredPois(out.pois.begin(), out.pois.begin() + static_cast<std::ptrdiff_t>(scored));
    std::vector<Poi> extra = detail::placeCoveragePois(roads, settlements, params, scoredPois, counted, params.coverageReserve, tieSeed);
    for (Poi& e : extra) {
        const PoiSightline s = poiSightline(roads, viewpoints, params, e.x, e.y);
        e.score = static_cast<std::uint16_t>(s.scorePermille);
        e.visibleCount = static_cast<std::uint16_t>(s.visibleCount);
    }

    std::vector<Poi> merged(out.pois.begin(), out.pois.begin() + static_cast<std::ptrdiff_t>(counted));
    merged.insert(merged.end(), extra.begin(), extra.end());
    merged.insert(merged.end(), out.pois.begin() + static_cast<std::ptrdiff_t>(counted), out.pois.begin() + static_cast<std::ptrdiff_t>(scored));
    for (std::size_t i = scored; i < out.pois.size() && static_cast<int>(merged.size()) < params.maxPois; ++i) {
        const Poi& later = out.pois[i];
        const bool crowded = std::any_of(extra.begin(), extra.end(), [&](const Poi& e) {
            const std::int64_t dx = e.x - later.x;
            const std::int64_t dy = e.y - later.y;
            return dx * dx + dy * dy < spacingSq;
        });
        if (!crowded) merged.push_back(later);
    }
    out.pois = std::move(merged);
    return out;
}

// Order-sensitive digest of every field of the map; the golden value that guards determinism.
inline std::uint64_t hashPoiMap(const PoiMap& map)
{
    std::uint64_t h = splitmix64(map.seed ^ detail::kPoiDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    mix(static_cast<std::uint64_t>(map.viewpointCount));
    mix(map.pois.size());
    for (const Poi& p : map.pois) {
        mix((static_cast<std::uint64_t>(static_cast<std::uint16_t>(p.x)) << 32) | (static_cast<std::uint64_t>(static_cast<std::uint16_t>(p.y)) << 16) | p.score);
        mix(p.visibleCount);
    }
    return h;
}

}
