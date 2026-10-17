#pragma once

#include <worldforge/hydrology_map.h>
#include <worldforge/subseed.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <numeric>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

// Phase 8, Roads (doc 08): roads between settlement sites, routed by A* and then built into the
// terrain by deforming it. Phase 9 onward consume RoadMap; none of them may mutate it. The input
// HydrologyMap is only read.
//
// Inputs:
//  - HydrologyMap: its heightM is the terrain the roads are routed over and deformed. Ocean cells
//    (heightM <= seaLevelM) and lake cells (lakeDepthM > 0) are impassable. River cells are
//    passable at a crossing cost (a ford or bridge).
//  - sites: plain (x, y) cells, one per settlement, in the grid of the map. Order is the site
//    index used in the output. SEED-128 produces these; this phase does not depend on its type.
//  - biomeCost: optional, one byte per cell (empty = no biome influence). 0 is free, 1..254 add
//    cost * biomeWeight to a step into the cell, 255 is impassable. The biome phase maps its
//    classes to this table; roads know nothing else about biomes.
//
// Output contract (grid shared with the HydrologyMap: width, height, row-major,
// index = y * width + x):
//  - heightM:  the terrain after deformation, in metres. Road cells are cut and filled to a smooth
//    profile (a moving average of the terrain along the road, radius smoothRadius), and their
//    non-road land neighbours (not river, not water) are pulled halfway toward the profile of the first road
//    cell that reaches them, once, as shoulders.
//    With smoothRadius 0 nothing is deformed and heightM equals the input. Never below
//    seaLevelM + 1 on cells that were land. Water cells are untouched.
//  - road:     0 off-road, 1 road, 2 road on a river cell (a ford or bridge).
//  - edges:    the road graph over site indices (a < b). A minimum spanning forest of the sites
//    over the A* route cost, then extra edges (extraEdgePermille of the tree edge count, cheapest
//    first) so the graph has loops. extra marks those. cost is the cost of the route as laid
//    (reusing an existing road is cheaper). Edges are listed tree edges first, then extras.
//  - pathCells / edge.pathBegin, pathCount: the route of each edge as grid indices from site a to
//    site b, 8-connected, contiguous in pathCells. This is the road spline's control polyline.
//  - components: number of connected groups of sites in the road graph. 1 means every site is
//    connected. More than 1 only when sites sit on separate land masses (or are hemmed in by
//    impassable cells) with no possible route.
//
// Algorithm:
//  - Cost of a step: 16 * (10 orthogonal, 14 diagonal) base, plus a quadratic slope penalty
//    (slopeWeight * slope^2, slope as rise over run in 1/256 steps using cellSizeM), plus
//    riverCost into a river cell, plus the biome cost. Stepping onto an existing road costs half
//    the base and nothing else, so later roads merge into earlier ones.
//  - Candidates are each site's candidateNeighbours nearest sites on the same land mass (squared
//    distance, ties by index), costed by A* with an octile heuristic. Kruskal picks the tree over
//    (cost, a, b); components left apart by the candidate set are joined by the nearest same land
//    mass pairs. The routes are then laid in order, deformed, and marked.
//
// Determinism: integer arithmetic only, no libm, no floating point, no unordered container; every
// priority queue key and sort key ends in a unique index, so the result is bit-identical on every
// platform. Memory is O(cells), about 40 bytes per cell of working state plus the search heap, with
// cells <= kTectonicsMaxSize^2, plus at most kRoadsMaxPathCells stored route cells. Time is bounded
// by kRoadsMaxExpansions search steps in total. The build fails with nullopt rather than exceed
// either limit.
namespace worldforge {

constexpr int kRoadsMaxSites = 128;
constexpr std::size_t kRoadsMaxPathCells = 1u << 22;
constexpr std::uint64_t kRoadsMaxExpansions = 1ull << 28;
constexpr std::uint8_t kRoadsImpassable = 255;
constexpr int kRoadsMaxCellSizeM = 1024;
constexpr int kRoadsMaxSlopeWeight = 1024;
constexpr int kRoadsMaxRiverCost = 1 << 20;
constexpr int kRoadsMaxBiomeWeight = 256;
constexpr int kRoadsMaxExtraPermille = 1000;
constexpr int kRoadsMaxCandidateNeighbours = 16;
constexpr int kRoadsMaxSmoothRadius = 64;

struct RoadSite {
    int x = 0;
    int y = 0;
};

struct RoadParams {
    int seaLevelM = 0;             // cells at or below this are ocean, in the tectonics height range
    int cellSizeM = 16;            // width of a cell in metres, in [1, kRoadsMaxCellSizeM]
    int slopeWeight = 64;          // penalty per squared slope, in [0, kRoadsMaxSlopeWeight]
    int riverCost = 2400;          // extra cost of stepping into a river cell, in [0, kRoadsMaxRiverCost]
    int biomeWeight = 8;           // multiplier of the biome cost byte, in [0, kRoadsMaxBiomeWeight]
    int extraEdgePermille = 150;   // extra edges as a share of the tree edges, in [0, kRoadsMaxExtraPermille]
    int candidateNeighbours = 6;   // nearest sites costed per site, in [1, kRoadsMaxCandidateNeighbours]
    int smoothRadius = 4;          // road profile averaging radius in cells, 0 disables deformation, in [0, kRoadsMaxSmoothRadius]
};

struct RoadEdge {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    bool extra = false;
    std::int64_t cost = 0;
    std::uint32_t pathBegin = 0;
    std::uint32_t pathCount = 0;
};

struct RoadMap {
    std::uint64_t seed = 0; // HydrologyMap::seed it was derived from
    int width = 0;
    int height = 0;
    std::vector<std::int16_t> heightM;
    std::vector<std::uint8_t> road;
    std::vector<RoadEdge> edges;
    std::vector<std::uint32_t> pathCells;
    int components = 0;

    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); }
};

namespace detail {

constexpr std::uint64_t kRoadsDomain = 0x726f6164735f3038ull;

inline bool roadsInputValid(const HydrologyMap& map)
{
    if (map.width < kTectonicsMinSize || map.width > kTectonicsMaxSize || map.height < kTectonicsMinSize || map.height > kTectonicsMaxSize) return false;
    const std::size_t cells = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
    if (map.heightM.size() != cells || map.lakeDepthM.size() != cells || map.river.size() != cells) return false;
    return std::all_of(map.heightM.begin(), map.heightM.end(), [](std::int16_t v) { return v >= kTectonicsMinHeightM && v <= kTectonicsMaxHeightM; });
}

inline bool roadsParamsValid(const RoadParams& p)
{
    return p.seaLevelM >= kTectonicsMinHeightM && p.seaLevelM <= kTectonicsMaxHeightM && p.cellSizeM >= 1 && p.cellSizeM <= kRoadsMaxCellSizeM
        && p.slopeWeight >= 0 && p.slopeWeight <= kRoadsMaxSlopeWeight && p.riverCost >= 0 && p.riverCost <= kRoadsMaxRiverCost
        && p.biomeWeight >= 0 && p.biomeWeight <= kRoadsMaxBiomeWeight && p.extraEdgePermille >= 0 && p.extraEdgePermille <= kRoadsMaxExtraPermille
        && p.candidateNeighbours >= 1 && p.candidateNeighbours <= kRoadsMaxCandidateNeighbours && p.smoothRadius >= 0 && p.smoothRadius <= kRoadsMaxSmoothRadius;
}

struct RoadRouter {
    const HydrologyMap& map;
    const RoadParams& params;
    const std::vector<std::uint8_t>& biome;
    const std::vector<std::uint8_t>& roadMask; // read only when routing with reuse
    int w;
    int h;
    std::size_t cells;
    std::vector<std::int64_t> g;
    std::vector<std::uint32_t> parent;
    std::vector<std::uint32_t> openStamp;
    std::vector<std::uint32_t> closedStamp;
    std::uint32_t stamp = 0;
    std::uint64_t expansions = 0;

    RoadRouter(const HydrologyMap& m, const RoadParams& p, const std::vector<std::uint8_t>& b, const std::vector<std::uint8_t>& roads)
        : map(m), params(p), biome(b), roadMask(roads), w(m.width), h(m.height), cells(m.heightM.size()), g(cells, 0), parent(cells, 0), openStamp(cells, 0), closedStamp(cells, 0)
    {
    }

    bool passable(std::size_t c) const
    {
        return map.heightM[c] > params.seaLevelM && map.lakeDepthM[c] == 0 && (biome.empty() || biome[c] != kRoadsImpassable);
    }

    std::int64_t stepCost(std::size_t c, std::size_t n, int dir, bool reuse) const
    {
        const std::int64_t len = (dir & 1) ? 14 : 10;
        const std::int64_t base = 16 * len;
        if (reuse && roadMask[n] != 0) return base / 2;
        const std::int64_t dz = std::abs(static_cast<std::int64_t>(map.heightM[n]) - static_cast<std::int64_t>(map.heightM[c]));
        const std::int64_t slope = dz * 2560 / (static_cast<std::int64_t>(params.cellSizeM) * len);
        std::int64_t cost = base + ((static_cast<std::int64_t>(params.slopeWeight) * slope * slope) >> 12);
        if (map.river[n] != 0) cost += params.riverCost;
        if (!biome.empty()) cost += static_cast<std::int64_t>(biome[n]) * params.biomeWeight;
        return cost;
    }

    std::int64_t heuristic(std::size_t c, std::size_t goal, bool reuse) const
    {
        const std::int64_t dx = std::abs(static_cast<std::int64_t>(c % static_cast<std::size_t>(w)) - static_cast<std::int64_t>(goal % static_cast<std::size_t>(w)));
        const std::int64_t dy = std::abs(static_cast<std::int64_t>(c / static_cast<std::size_t>(w)) - static_cast<std::int64_t>(goal / static_cast<std::size_t>(w)));
        return (reuse ? 8 : 16) * (10 * std::max(dx, dy) + 4 * std::min(dx, dy));
    }

    // Cheapest route cost from `from` to `to`, nullopt when none exists. Fills `path` (from..to)
    // when given.
    std::optional<std::int64_t> route(std::size_t from, std::size_t to, bool reuse, std::vector<std::uint32_t>* path)
    {
        ++stamp;
        using Entry = std::pair<std::int64_t, std::uint32_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
        g[from] = 0;
        openStamp[from] = stamp;
        open.emplace(heuristic(from, to, reuse), static_cast<std::uint32_t>(from));
        while (!open.empty()) {
            const std::size_t c = open.top().second;
            open.pop();
            if (closedStamp[c] == stamp) continue;
            closedStamp[c] = stamp;
            if (++expansions > kRoadsMaxExpansions) return std::nullopt;
            if (c == to) {
                if (path != nullptr) {
                    path->clear();
                    for (std::size_t at = to;; at = parent[at]) {
                        path->push_back(static_cast<std::uint32_t>(at));
                        if (at == from) break;
                    }
                    std::reverse(path->begin(), path->end());
                }
                return g[c];
            }
            const int cx = static_cast<int>(c % static_cast<std::size_t>(w));
            const int cy = static_cast<int>(c / static_cast<std::size_t>(w));
            for (int k = 0; k < 8; ++k) {
                const int nx = cx + kHydrologyDx[k];
                const int ny = cy + kHydrologyDy[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const std::size_t n = static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx);
                if (closedStamp[n] == stamp || !passable(n)) continue;
                const std::int64_t cand = g[c] + stepCost(c, n, k, reuse);
                if (openStamp[n] == stamp && cand >= g[n]) continue;
                openStamp[n] = stamp;
                g[n] = cand;
                parent[n] = static_cast<std::uint32_t>(c);
                open.emplace(cand + heuristic(n, to, reuse), static_cast<std::uint32_t>(n));
            }
        }
        return std::nullopt;
    }
};

inline int roadsFind(std::vector<int>& dsu, int v)
{
    while (dsu[static_cast<std::size_t>(v)] != v) {
        dsu[static_cast<std::size_t>(v)] = dsu[static_cast<std::size_t>(dsu[static_cast<std::size_t>(v)])];
        v = dsu[static_cast<std::size_t>(v)];
    }
    return v;
}

struct RoadCandidate {
    std::int64_t cost = 0;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    bool operator<(const RoadCandidate& o) const
    {
        if (cost != o.cost) return cost < o.cost;
        if (a != o.a) return a < o.a;
        return b < o.b;
    }
};

}

// Returns nullopt when the map is malformed, a parameter is out of range, the site list is empty,
// longer than kRoadsMaxSites, has a duplicate, or has a site off the grid, in the sea, in a lake or
// in an impassable biome cell, when biomeCost is non-empty with the wrong length, or when the
// routes would exceed kRoadsMaxPathCells or the search kRoadsMaxExpansions. Otherwise a pure function of (map, sites, biomeCost,
// params).
inline std::optional<RoadMap> computeRoads(const HydrologyMap& map, const std::vector<RoadSite>& sites, const RoadParams& params = {}, const std::vector<std::uint8_t>& biomeCost = {})
{
    if (!detail::roadsInputValid(map) || !detail::roadsParamsValid(params)) return std::nullopt;
    const int w = map.width;
    const int h = map.height;
    const std::size_t cells = map.heightM.size();
    if (!biomeCost.empty() && biomeCost.size() != cells) return std::nullopt;
    if (sites.empty() || sites.size() > static_cast<std::size_t>(kRoadsMaxSites)) return std::nullopt;

    RoadMap out;
    out.road.assign(cells, 0);
    detail::RoadRouter router(map, params, biomeCost, out.road);

    const int n = static_cast<int>(sites.size());
    std::vector<std::size_t> cell(sites.size());
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const RoadSite& s = sites[i];
        if (s.x < 0 || s.y < 0 || s.x >= w || s.y >= h) return std::nullopt;
        cell[i] = static_cast<std::size_t>(s.y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(s.x);
        if (!router.passable(cell[i])) return std::nullopt;
    }
    {
        std::vector<std::size_t> sorted = cell;
        std::sort(sorted.begin(), sorted.end());
        if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) return std::nullopt;
    }

    // Land mass label per cell (8-connected passable cells), 0 for impassable.
    std::vector<std::uint32_t> label(cells, 0);
    {
        std::uint32_t next = 0;
        std::vector<std::uint32_t> stack;
        for (std::size_t i = 0; i < sites.size(); ++i) {
            if (label[cell[i]] != 0) continue;
            ++next;
            label[cell[i]] = next;
            stack.push_back(static_cast<std::uint32_t>(cell[i]));
            while (!stack.empty()) {
                const std::size_t c = stack.back();
                stack.pop_back();
                const int cx = static_cast<int>(c % static_cast<std::size_t>(w));
                const int cy = static_cast<int>(c / static_cast<std::size_t>(w));
                for (int k = 0; k < 8; ++k) {
                    const int nx = cx + detail::kHydrologyDx[k];
                    const int ny = cy + detail::kHydrologyDy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const std::size_t m = static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx);
                    if (label[m] != 0 || !router.passable(m)) continue;
                    label[m] = next;
                    stack.push_back(static_cast<std::uint32_t>(m));
                }
            }
        }
    }

    const auto dist2 = [&](int i, int j) {
        const std::int64_t dx = sites[static_cast<std::size_t>(i)].x - sites[static_cast<std::size_t>(j)].x;
        const std::int64_t dy = sites[static_cast<std::size_t>(i)].y - sites[static_cast<std::size_t>(j)].y;
        return dx * dx + dy * dy;
    };
    const auto sameLand = [&](int i, int j) { return label[cell[static_cast<std::size_t>(i)]] == label[cell[static_cast<std::size_t>(j)]]; };

    std::vector<std::pair<int, int>> pairs;
    for (int i = 0; i < n; ++i) {
        std::vector<std::pair<std::int64_t, int>> near;
        for (int j = 0; j < n; ++j) {
            if (j != i && sameLand(i, j)) near.emplace_back(dist2(i, j), j);
        }
        std::sort(near.begin(), near.end());
        if (near.size() > static_cast<std::size_t>(params.candidateNeighbours)) near.resize(static_cast<std::size_t>(params.candidateNeighbours));
        for (const auto& entry : near) pairs.emplace_back(std::min(i, entry.second), std::max(i, entry.second));
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());

    std::vector<detail::RoadCandidate> candidates;
    candidates.reserve(pairs.size());
    for (const auto& pr : pairs) {
        const auto cost = router.route(cell[static_cast<std::size_t>(pr.first)], cell[static_cast<std::size_t>(pr.second)], false, nullptr);
        if (!cost) return std::nullopt;
        candidates.push_back({ *cost, static_cast<std::uint32_t>(pr.first), static_cast<std::uint32_t>(pr.second) });
    }
    std::sort(candidates.begin(), candidates.end());

    std::vector<int> dsu(sites.size());
    std::iota(dsu.begin(), dsu.end(), 0);
    std::vector<detail::RoadCandidate> tree;
    std::vector<detail::RoadCandidate> rest;
    for (const auto& c : candidates) {
        const int ra = detail::roadsFind(dsu, static_cast<int>(c.a));
        const int rb = detail::roadsFind(dsu, static_cast<int>(c.b));
        if (ra == rb) {
            rest.push_back(c);
            continue;
        }
        dsu[static_cast<std::size_t>(std::max(ra, rb))] = std::min(ra, rb);
        tree.push_back(c);
    }

    {
        std::vector<std::pair<std::int64_t, std::pair<int, int>>> joins;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                if (sameLand(i, j) && detail::roadsFind(dsu, i) != detail::roadsFind(dsu, j)) joins.push_back({ dist2(i, j), { i, j } });
            }
        }
        std::sort(joins.begin(), joins.end());
        for (const auto& jn : joins) {
            const int ra = detail::roadsFind(dsu, jn.second.first);
            const int rb = detail::roadsFind(dsu, jn.second.second);
            if (ra == rb) continue;
            const auto cost = router.route(cell[static_cast<std::size_t>(jn.second.first)], cell[static_cast<std::size_t>(jn.second.second)], false, nullptr);
            if (!cost) return std::nullopt;
            dsu[static_cast<std::size_t>(std::max(ra, rb))] = std::min(ra, rb);
            tree.push_back({ *cost, static_cast<std::uint32_t>(jn.second.first), static_cast<std::uint32_t>(jn.second.second) });
        }
    }

    int components = 0;
    for (int i = 0; i < n; ++i) {
        if (detail::roadsFind(dsu, i) == i) ++components;
    }

    const std::size_t extraWanted = (tree.size() * static_cast<std::size_t>(params.extraEdgePermille) + 500) / 1000;
    const std::size_t extraCount = std::min(extraWanted, rest.size());

    out.seed = map.seed;
    out.width = w;
    out.height = h;
    out.components = components;

    std::vector<std::pair<detail::RoadCandidate, bool>> chosen;
    for (const auto& c : tree) chosen.emplace_back(c, false);
    for (std::size_t i = 0; i < extraCount; ++i) chosen.emplace_back(rest[i], true);

    std::vector<std::uint32_t> path;
    for (const auto& entry : chosen) {
        const auto cost = router.route(cell[entry.first.a], cell[entry.first.b], true, &path);
        if (!cost) return std::nullopt;
        if (out.pathCells.size() + path.size() > kRoadsMaxPathCells) return std::nullopt;
        RoadEdge edge;
        edge.a = entry.first.a;
        edge.b = entry.first.b;
        edge.extra = entry.second;
        edge.cost = *cost;
        edge.pathBegin = static_cast<std::uint32_t>(out.pathCells.size());
        edge.pathCount = static_cast<std::uint32_t>(path.size());
        out.pathCells.insert(out.pathCells.end(), path.begin(), path.end());
        for (const std::uint32_t c : path) out.road[c] = map.river[c] != 0 ? 2 : 1;
        out.edges.push_back(edge);
    }

    out.heightM = map.heightM;
    if (params.smoothRadius > 0) {
        const int r = params.smoothRadius;
        const std::int64_t lo = static_cast<std::int64_t>(params.seaLevelM) + 1;
        std::vector<std::int16_t> profile;
        std::vector<std::uint8_t> shouldered(cells, 0);
        for (const RoadEdge& edge : out.edges) {
            const std::int64_t len = edge.pathCount;
            const std::uint32_t* cellsOnPath = out.pathCells.data() + edge.pathBegin;
            profile.assign(static_cast<std::size_t>(len), 0);
            for (std::int64_t i = 0; i < len; ++i) {
                const std::int64_t from = std::max<std::int64_t>(0, i - r);
                const std::int64_t to = std::min<std::int64_t>(len - 1, i + r);
                std::int64_t sum = 0;
                for (std::int64_t j = from; j <= to; ++j) sum += out.heightM[cellsOnPath[j]];
                const std::int64_t count = to - from + 1;
                const std::int64_t avg = sum >= 0 ? (sum + count / 2) / count : -((-sum + count / 2) / count);
                profile[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::clamp<std::int64_t>(avg, lo, kTectonicsMaxHeightM));
            }
            for (std::int64_t i = 0; i < len; ++i) {
                const std::size_t c = cellsOnPath[i];
                const std::int16_t target = profile[static_cast<std::size_t>(i)];
                const int cx = static_cast<int>(c % static_cast<std::size_t>(w));
                const int cy = static_cast<int>(c / static_cast<std::size_t>(w));
                for (int k = 0; k < 8; ++k) {
                    const int nx = cx + detail::kHydrologyDx[k];
                    const int ny = cy + detail::kHydrologyDy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const std::size_t m = static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx);
                    if (out.road[m] != 0 || shouldered[m] != 0 || map.river[m] != 0 || map.heightM[m] <= params.seaLevelM || map.lakeDepthM[m] != 0) continue;
                    shouldered[m] = 1;
                    const std::int64_t mixed = (static_cast<std::int64_t>(out.heightM[m]) + target) >> 1;
                    out.heightM[m] = static_cast<std::int16_t>(std::max<std::int64_t>(mixed, lo));
                }
            }
            for (std::int64_t i = 0; i < len; ++i) out.heightM[cellsOnPath[i]] = profile[static_cast<std::size_t>(i)];
        }
    }
    return out;
}

// Order-sensitive digest of every field of the map; the golden value that guards determinism.
inline std::uint64_t hashRoadMap(const RoadMap& map)
{
    std::uint64_t h = splitmix64(map.seed ^ detail::kRoadsDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    mix(static_cast<std::uint64_t>(map.components));
    const std::size_t cells = std::min(map.heightM.size(), map.road.size());
    for (std::size_t c = 0; c < cells; ++c) mix(static_cast<std::uint64_t>(static_cast<std::uint16_t>(map.heightM[c])) | (static_cast<std::uint64_t>(map.road[c]) << 16));
    for (const RoadEdge& e : map.edges) {
        mix((static_cast<std::uint64_t>(e.a) << 32) | e.b);
        mix(static_cast<std::uint64_t>(e.cost));
        mix((static_cast<std::uint64_t>(e.pathBegin) << 32) | e.pathCount | (e.extra ? 1ull << 63 : 0));
    }
    for (const std::uint32_t c : map.pathCells) mix(c);
    return h;
}

}
