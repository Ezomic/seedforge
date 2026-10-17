#pragma once

#include <worldforge/erosion_map.h>
#include <worldforge/subseed.h>
#include <worldforge/tectonics_map.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <vector>

// Phase 3, Hydrology (doc 08): priority-flood depression filling, D8 flow accumulation, rivers
// above an accumulation threshold, and channels carved back into the terrain. Phases 4 to 6
// consume HydrologyMap; none of them may mutate it. The input ErodedMap is never modified.
//
// Output contract (all fields share the ErodedMap grid: width, height, row-major,
// index = y * width + x):
//  - filledM:    eroded elevation with every depression raised to its spill height, so water
//                surfaces are filledM. In metres, within the tectonics height range.
//  - lakeDepthM: filledM minus the eroded height, > 0 exactly on lake cells (filled depressions).
//  - heightM:    final terrain in metres: the eroded height with river channels carved in. Equals
//                the eroded height on every non-river and every lake cell. A carved cell never
//                drops below seaLevelM + 1, so rivers stay land.
//  - flowDir:    D8 receiver of each cell, 0..7 indexing the neighbour table
//                (dx, dy) = (1,0) (1,1) (0,1) (-1,1) (-1,0) (-1,-1) (0,-1) (1,-1), or
//                kHydrologyNoFlow for outlets: ocean cells (eroded height <= seaLevelM) and
//                cells on the map border, where water leaves the map. Following flowDir from any
//                cell reaches an outlet; there are no cycles and no interior sinks. The receiver's
//                filledM is never higher than the cell's own.
//  - accumulation: number of cells (itself included) draining through the cell, each cell
//                contributing one unit of rain. Summed over the outlets it equals width * height.
//  - river:      1 on land cells with accumulation >= riverThreshold, else 0. Ocean cells are
//                never rivers.
//
// Algorithm: priority-flood from the outlets (a binary heap keyed by (filled height, index)); each
// cell is reached once and its receiver is the cell it was reached from, which makes the flow
// graph a forest rooted at the outlets by construction. Accumulation sums children into parents in
// reverse visit order.
//
// Determinism: integer only, no libm, no floating point, no unordered container; heap keys are
// unique, so the result is bit-identical on every platform. Memory is O(cells), at most about 40
// bytes per cell with cells <= kTectonicsMaxSize^2.
namespace worldforge {

constexpr std::uint8_t kHydrologyNoFlow = 255;
constexpr std::int64_t kHydrologyMaxRiverThreshold = 1 << 24;
constexpr int kHydrologyMaxCarveRate = 256;
constexpr int kHydrologyMaxCarveM = 1000;

struct HydrologyParams {
    int seaLevelM = 0;               // cells at or below this are ocean outlets, in the tectonics height range
    std::int64_t riverThreshold = 48; // accumulation (cells) from which a land cell is a river, in [1, kHydrologyMaxRiverThreshold]
    int carveRate = 64;              // channel depth per sqrt(accumulation), in 1/256 m, in [0, kHydrologyMaxCarveRate]
    int maxCarveM = 40;              // deepest channel, in [0, kHydrologyMaxCarveM]
};

struct HydrologyMap {
    std::uint64_t seed = 0; // ErodedMap::seed it was derived from
    int width = 0;
    int height = 0;
    std::vector<std::int16_t> filledM;
    std::vector<std::uint16_t> lakeDepthM;
    std::vector<std::int16_t> heightM;
    std::vector<std::uint8_t> flowDir;
    std::vector<std::uint32_t> accumulation;
    std::vector<std::uint8_t> river;

    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); }
};

namespace detail {

constexpr std::uint64_t kHydrologyDomain = 0x6879647230336d70ull;
constexpr int kHydrologyDx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
constexpr int kHydrologyDy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

inline bool hydrologyInputValid(const ErodedMap& map)
{
    if (map.width < kTectonicsMinSize || map.width > kTectonicsMaxSize || map.height < kTectonicsMinSize || map.height > kTectonicsMaxSize) return false;
    if (map.heightM.size() != static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height)) return false;
    return std::all_of(map.heightM.begin(), map.heightM.end(), [](std::int16_t v) { return v >= kTectonicsMinHeightM && v <= kTectonicsMaxHeightM; });
}

inline bool hydrologyParamsValid(const HydrologyParams& p)
{
    return p.seaLevelM >= kTectonicsMinHeightM && p.seaLevelM <= kTectonicsMaxHeightM && p.riverThreshold >= 1 && p.riverThreshold <= kHydrologyMaxRiverThreshold
        && p.carveRate >= 0 && p.carveRate <= kHydrologyMaxCarveRate && p.maxCarveM >= 0 && p.maxCarveM <= kHydrologyMaxCarveM;
}

}

// Returns nullopt when the map is malformed (size outside the kTectonics* limits, a height field
// of the wrong length or with a height outside the allowed range) or a parameter is out of range.
// Otherwise a pure function of (map.seed, map.width, map.height, map.heightM, params).
inline std::optional<HydrologyMap> computeHydrology(const ErodedMap& map, const HydrologyParams& params = {})
{
    if (!detail::hydrologyInputValid(map) || !detail::hydrologyParamsValid(params)) return std::nullopt;

    const int w = map.width;
    const int h = map.height;
    const std::size_t cells = map.heightM.size();
    const std::vector<std::int16_t>& z = map.heightM;

    HydrologyMap out;
    out.seed = map.seed;
    out.width = w;
    out.height = h;
    out.filledM = z;
    out.flowDir.assign(cells, kHydrologyNoFlow);

    std::vector<std::uint32_t> parent(cells, 0);
    std::vector<std::uint32_t> order;
    order.reserve(cells);
    std::vector<std::uint8_t> queued(cells, 0);
    std::priority_queue<std::uint64_t, std::vector<std::uint64_t>, std::greater<std::uint64_t>> heap;
    const auto push = [&](std::size_t c) {
        const std::uint64_t level = static_cast<std::uint64_t>(static_cast<std::int64_t>(out.filledM[c]) - kTectonicsMinHeightM);
        heap.push((level << 32) | static_cast<std::uint64_t>(c));
        queued[c] = 1;
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t c = out.index(x, y);
            if (z[c] <= params.seaLevelM || x == 0 || y == 0 || x == w - 1 || y == h - 1) push(c);
        }
    }

    while (!heap.empty()) {
        const std::size_t c = static_cast<std::size_t>(heap.top() & 0xFFFFFFFFu);
        heap.pop();
        order.push_back(static_cast<std::uint32_t>(c));
        const int cx = static_cast<int>(c % static_cast<std::size_t>(w));
        const int cy = static_cast<int>(c / static_cast<std::size_t>(w));
        for (int k = 0; k < 8; ++k) {
            const int nx = cx + detail::kHydrologyDx[k];
            const int ny = cy + detail::kHydrologyDy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const std::size_t n = out.index(nx, ny);
            if (queued[n]) continue;
            out.filledM[n] = std::max(z[n], out.filledM[c]);
            out.flowDir[n] = static_cast<std::uint8_t>((k + 4) & 7);
            parent[n] = static_cast<std::uint32_t>(c);
            push(n);
        }
    }

    out.accumulation.assign(cells, 1);
    for (std::size_t i = order.size(); i-- > 0;) {
        const std::size_t c = order[i];
        if (out.flowDir[c] != kHydrologyNoFlow) out.accumulation[parent[c]] += out.accumulation[c];
    }

    out.lakeDepthM.resize(cells);
    out.river.assign(cells, 0);
    out.heightM = z;
    for (const std::uint32_t c : order) {
        out.lakeDepthM[c] = static_cast<std::uint16_t>(out.filledM[c] - z[c]);
        if (z[c] <= params.seaLevelM || out.accumulation[c] < static_cast<std::uint64_t>(params.riverThreshold)) continue;
        out.river[c] = 1;
        if (out.lakeDepthM[c] != 0) continue;
        const std::int64_t depth = std::min<std::int64_t>(params.maxCarveM, static_cast<std::int64_t>(detail::isqrtU64(out.accumulation[c])) * params.carveRate >> 8);
        out.heightM[c] = static_cast<std::int16_t>(std::max<std::int64_t>(static_cast<std::int64_t>(z[c]) - depth, static_cast<std::int64_t>(params.seaLevelM) + 1));
    }
    return out;
}

// Order-sensitive digest of every field of the map; the golden value that guards determinism.
inline std::uint64_t hashHydrologyMap(const HydrologyMap& map)
{
    std::uint64_t h = splitmix64(map.seed ^ detail::kHydrologyDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    const std::size_t cells = std::min({ map.filledM.size(), map.lakeDepthM.size(), map.heightM.size(), map.flowDir.size(), map.accumulation.size(), map.river.size() });
    for (std::size_t c = 0; c < cells; ++c) {
        mix(static_cast<std::uint64_t>(static_cast<std::uint16_t>(map.filledM[c])) | (static_cast<std::uint64_t>(static_cast<std::uint16_t>(map.heightM[c])) << 16)
            | (static_cast<std::uint64_t>(map.lakeDepthM[c]) << 32) | (static_cast<std::uint64_t>(map.flowDir[c]) << 48)
            | (static_cast<std::uint64_t>(map.river[c]) << 56));
        mix(map.accumulation[c]);
    }
    return h;
}

}
