#pragma once

#include <worldforge/erosion_map.h>
#include <worldforge/subseed.h>
#include <worldforge/tectonics_map.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <vector>

// Phase 6, Resources (doc 08): ore grade and kind per cell from tectonic boundaries, ley nodes on
// plate boundaries when the Law calls for them, and an arable score from slope, elevation,
// moisture and erosion deposits. Phase 7 (settlements) and phase 9 (politics) consume ResourceMap;
// none of them may mutate it. The inputs are never modified.
//
// Output contract (ResourceMap, same grid as the inputs, row-major, index = y * width + x):
//  - oreKind[c]  : OreKind. Orichalcum near convergent boundaries, Iron near divergent ones, None
//                  elsewhere. Only land cells (eroded height >= 0) carry ore.
//  - oreGrade[c] : 0 (no ore) to 255 (richest). oreKind is None iff oreGrade is 0.
//  - arable[c]   : 0 (unfarmable, including all ocean) to 255 (best farmland).
//  - leyNodes    : at most params.leyNodeCount cells, each on a plate boundary cell, in placement
//                  order (strongest candidate first), pairwise at least params.leyMinSpacing apart
//                  (Euclidean, in cells). Empty when the Law does not call for ley nodes.
//
// Deferred inputs: hydrology (phase 3), climate (phase 4) and biomes (phase 5) are not on main.
// The arable score's moisture term reads ResourceInputs::moisture, one byte per cell (0 dry to 255
// wet). An empty span means "not available yet" and every cell gets kNeutralMoisture; once phase 4
// lands it fills the span and nothing else here changes. Water proximity and biome modulation of
// the arable score are likewise deferred.
//
// Determinism: integer-only, no libm, no floating point, no unordered container; ties resolve by
// row-major cell index. Randomness comes only from subseed.h hashing. Memory is O(cells), at most
// about 12 bytes per cell, with cells <= kTectonicsMaxSize^2.
namespace worldforge {

enum class OreKind : std::uint8_t { None, Orichalcum, Iron };

constexpr int kResourceMaxOreReach = 255;
constexpr int kResourceMaxLeyNodes = 64;
constexpr int kResourceMaxLeySpacing = 2048;
constexpr std::uint8_t kNeutralMoisture = 128;

struct ResourceParams {
    int oreReach = 12;      // cells from a boundary at which ore fades out, in [1, 255]
    int minOreGrade = 24;   // grades below this are dropped, in [0, 255]
    int leyNodeCount = 0;   // 0 when the Law does not call for ley nodes, in [0, 64]
    int leyMinSpacing = 24; // minimum distance between ley nodes in cells, in [0, 2048]
};

struct ResourceInputs {
    const TectonicMap* tectonics = nullptr;
    const ErodedMap* eroded = nullptr;
    std::span<const std::uint8_t> moisture; // empty, or width * height bytes
};

struct LeyNode {
    std::int16_t x = 0;
    std::int16_t y = 0;
};

struct ResourceMap {
    std::uint64_t seed = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> oreKind;
    std::vector<std::uint8_t> oreGrade;
    std::vector<std::uint8_t> arable;
    std::vector<LeyNode> leyNodes;

    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); }
};

namespace detail {

constexpr std::uint64_t kResourceDomain = 0x7265736f75726336ull;

enum ResourceSalt : std::uint32_t { kSaltVein = 1, kSaltLey = 2 };

constexpr std::uint64_t resourceHash(std::uint64_t seed, std::uint32_t salt, std::uint32_t cell)
{
    return splitmix64(splitmix64(seed ^ kResourceDomain) ^ hashCell(static_cast<std::int32_t>(salt), static_cast<std::int32_t>(cell)));
}

constexpr std::uint16_t kResourceUnreached = 0xFFFF;
constexpr int kResourceSlopeCapM = 60;
constexpr int kResourceLowlandM = 600;
constexpr int kResourceHighlandM = 2400;
constexpr int kResourceMoisturePeak = 160;
constexpr int kResourceAlluvialMaxBonus = 40;

// 4-neighbour breadth-first distance to the nearest boundary cell of `kind`, capped below `reach`.
inline std::vector<std::uint16_t> boundaryDistance(const TectonicMap& map, BoundaryKind kind, int reach)
{
    const std::size_t cells = map.boundary.size();
    std::vector<std::uint16_t> dist(cells, kResourceUnreached);
    std::vector<std::uint32_t> queue;
    queue.reserve(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        if (static_cast<BoundaryKind>(map.boundary[c]) == kind) {
            dist[c] = 0;
            queue.push_back(static_cast<std::uint32_t>(c));
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::uint32_t c = queue[head];
        const int d = dist[c];
        if (d + 1 >= reach) continue;
        const int x = static_cast<int>(c % static_cast<std::uint32_t>(map.width));
        const int y = static_cast<int>(c / static_cast<std::uint32_t>(map.width));
        for (int n = 0; n < 4; ++n) {
            const int nx = x + (n == 0 ? 1 : n == 1 ? -1 : 0);
            const int ny = y + (n == 2 ? 1 : n == 3 ? -1 : 0);
            if (nx < 0 || ny < 0 || nx >= map.width || ny >= map.height) continue;
            const std::size_t nc = static_cast<std::size_t>(ny) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(nx);
            if (dist[nc] != kResourceUnreached) continue;
            dist[nc] = static_cast<std::uint16_t>(d + 1);
            queue.push_back(static_cast<std::uint32_t>(nc));
        }
    }
    return dist;
}

// Quadratic falloff: 255 on the boundary, 0 at `reach`.
inline std::int64_t oreFalloff(std::uint16_t d, int reach)
{
    if (d == kResourceUnreached || d >= reach) return 0;
    const std::int64_t lin = static_cast<std::int64_t>(reach - d) * 255 / reach;
    return lin * lin / 255;
}

inline bool resourceInputValid(const ResourceInputs& in)
{
    if (in.tectonics == nullptr || in.eroded == nullptr) return false;
    const TectonicMap& t = *in.tectonics;
    const ErodedMap& e = *in.eroded;
    if (t.width < kTectonicsMinSize || t.width > kTectonicsMaxSize || t.height < kTectonicsMinSize || t.height > kTectonicsMaxSize) return false;
    const std::size_t cells = static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height);
    if (t.heightM.size() != cells || t.boundary.size() != cells || t.plateId.size() != cells) return false;
    if (e.width != t.width || e.height != t.height || e.seed != t.seed || e.heightM.size() != cells) return false;
    if (!in.moisture.empty() && in.moisture.size() != cells) return false;
    const auto inRange = [](std::int16_t v) { return v >= kTectonicsMinHeightM && v <= kTectonicsMaxHeightM; };
    if (!std::all_of(t.heightM.begin(), t.heightM.end(), inRange) || !std::all_of(e.heightM.begin(), e.heightM.end(), inRange)) return false;
    return std::all_of(t.boundary.begin(), t.boundary.end(), [](std::uint8_t b) { return b <= static_cast<std::uint8_t>(BoundaryKind::Transform); });
}

inline bool resourceParamsValid(const ResourceParams& p)
{
    return p.oreReach >= 1 && p.oreReach <= kResourceMaxOreReach && p.minOreGrade >= 0 && p.minOreGrade <= 255 && p.leyNodeCount >= 0
        && p.leyNodeCount <= kResourceMaxLeyNodes && p.leyMinSpacing >= 0 && p.leyMinSpacing <= kResourceMaxLeySpacing;
}

inline void placeOre(const ResourceInputs& in, const ResourceParams& p, ResourceMap& out)
{
    const TectonicMap& t = *in.tectonics;
    const std::vector<std::uint16_t> conv = boundaryDistance(t, BoundaryKind::Convergent, p.oreReach);
    const std::vector<std::uint16_t> div = boundaryDistance(t, BoundaryKind::Divergent, p.oreReach);
    for (std::size_t c = 0; c < t.heightM.size(); ++c) {
        if (in.eroded->heightM[c] < 0) continue;
        const std::int64_t fc = oreFalloff(conv[c], p.oreReach);
        const std::int64_t fd = oreFalloff(div[c], p.oreReach) / 2;
        const std::int64_t falloff = std::max(fc, fd);
        if (falloff == 0) continue;
        const std::int64_t vein = static_cast<std::int64_t>(resourceHash(t.seed, kSaltVein, static_cast<std::uint32_t>(c)) & 255u);
        std::int64_t grade = falloff * vein / 255;
        const int exposed = t.heightM[c] - in.eroded->heightM[c];
        if (exposed > 0) grade += std::min(32, exposed / 4);
        grade = std::min<std::int64_t>(grade, 255);
        if (grade < p.minOreGrade || grade == 0) continue;
        out.oreGrade[c] = static_cast<std::uint8_t>(grade);
        out.oreKind[c] = static_cast<std::uint8_t>(fc >= fd ? OreKind::Orichalcum : OreKind::Iron);
    }
}

inline void scoreArable(const ResourceInputs& in, ResourceMap& out)
{
    const TectonicMap& t = *in.tectonics;
    const std::vector<std::int16_t>& z = in.eroded->heightM;
    const int w = t.width;
    for (int y = 0; y < t.height; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t c = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const int h = z[c];
            if (h < 0) continue;
            int slope = 0;
            if (x > 0) slope = std::max(slope, std::abs(h - z[c - 1]));
            if (x + 1 < w) slope = std::max(slope, std::abs(h - z[c + 1]));
            if (y > 0) slope = std::max(slope, std::abs(h - z[c - static_cast<std::size_t>(w)]));
            if (y + 1 < t.height) slope = std::max(slope, std::abs(h - z[c + static_cast<std::size_t>(w)]));
            const std::int64_t slopeScore = std::max(0, 255 - slope * 255 / kResourceSlopeCapM);
            const std::int64_t elevScore = h <= kResourceLowlandM ? 255
                : h >= kResourceHighlandM ? 0
                : static_cast<std::int64_t>(kResourceHighlandM - h) * 255 / (kResourceHighlandM - kResourceLowlandM);
            const int m = in.moisture.empty() ? kNeutralMoisture : in.moisture[c];
            const std::int64_t moistScore = m <= kResourceMoisturePeak ? static_cast<std::int64_t>(m) * 255 / kResourceMoisturePeak
                                                                       : 255 - static_cast<std::int64_t>(m - kResourceMoisturePeak) * 191 / (255 - kResourceMoisturePeak);
            std::int64_t score = slopeScore * elevScore / 255 * moistScore / 255;
            const int deposited = h - t.heightM[c];
            if (score > 0 && deposited > 0) score += std::min(kResourceAlluvialMaxBonus, deposited * 4);
            out.arable[c] = static_cast<std::uint8_t>(std::min<std::int64_t>(score, 255));
        }
    }
}

inline void placeLeyNodes(const TectonicMap& t, const ResourceParams& p, ResourceMap& out)
{
    if (p.leyNodeCount == 0) return;
    std::vector<std::uint64_t> keys;
    for (std::size_t c = 0; c < t.boundary.size(); ++c) {
        const int rank = boundaryRank(static_cast<BoundaryKind>(t.boundary[c]));
        if (rank == 0) continue;
        const std::uint64_t noise = resourceHash(t.seed, kSaltLey, static_cast<std::uint32_t>(c)) & 0xFFFFFFu;
        keys.push_back((static_cast<std::uint64_t>(rank) << 56) | (noise << 32) | static_cast<std::uint64_t>(c));
    }
    std::sort(keys.begin(), keys.end(), [](std::uint64_t a, std::uint64_t b) { return a > b; });
    const std::int64_t minSq = static_cast<std::int64_t>(p.leyMinSpacing) * p.leyMinSpacing;
    for (const std::uint64_t key : keys) {
        if (static_cast<int>(out.leyNodes.size()) >= p.leyNodeCount) break;
        const std::int64_t c = static_cast<std::int64_t>(key & 0xFFFFFFFFu);
        const std::int64_t x = c % t.width;
        const std::int64_t y = c / t.width;
        const bool clear = std::all_of(out.leyNodes.begin(), out.leyNodes.end(), [&](const LeyNode& n) {
            const std::int64_t dx = n.x - x;
            const std::int64_t dy = n.y - y;
            return dx * dx + dy * dy >= minSq;
        });
        if (clear) out.leyNodes.push_back({ static_cast<std::int16_t>(x), static_cast<std::int16_t>(y) });
    }
}

}

// Returns nullopt when an input is missing or malformed (null maps, size outside the kTectonics*
// limits, vectors of the wrong length, heights out of range, eroded map from another seed or grid,
// a moisture span that is neither empty nor width * height long), or a parameter is out of range.
// Otherwise a pure function of (tectonics, eroded, moisture, params).
inline std::optional<ResourceMap> generateResources(const ResourceInputs& inputs, const ResourceParams& params = {})
{
    if (!detail::resourceInputValid(inputs) || !detail::resourceParamsValid(params)) return std::nullopt;
    const TectonicMap& t = *inputs.tectonics;
    const std::size_t cells = t.heightM.size();
    ResourceMap out;
    out.seed = t.seed;
    out.width = t.width;
    out.height = t.height;
    out.oreKind.assign(cells, static_cast<std::uint8_t>(OreKind::None));
    out.oreGrade.assign(cells, 0);
    out.arable.assign(cells, 0);
    detail::placeOre(inputs, params, out);
    detail::scoreArable(inputs, out);
    detail::placeLeyNodes(t, params, out);
    return out;
}

// Order-sensitive digest of every field of the map; the golden value that guards determinism.
inline std::uint64_t hashResourceMap(const ResourceMap& map)
{
    std::uint64_t h = splitmix64(map.seed ^ detail::kResourceDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    for (std::size_t c = 0; c < map.oreGrade.size(); ++c) {
        mix((static_cast<std::uint64_t>(map.oreKind[c]) << 16) | (static_cast<std::uint64_t>(map.oreGrade[c]) << 8) | map.arable[c]);
    }
    mix(map.leyNodes.size());
    for (const LeyNode& n : map.leyNodes) mix((static_cast<std::uint64_t>(static_cast<std::uint16_t>(n.x)) << 16) | static_cast<std::uint16_t>(n.y));
    return h;
}

}
