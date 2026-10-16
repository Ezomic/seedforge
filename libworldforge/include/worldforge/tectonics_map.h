#pragma once

#include <worldforge/subseed.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Phase 1, Tectonics (doc 08): a deterministic plate map and a coarse base height field, on an
// integer grid. Phases 2 to 6 consume TectonicMap; none of them may mutate it.
//
// Determinism: integer math only (int64 squared distances, integer lattice noise, a multi-source
// breadth-first distance field). There is no libm, no floating point and no unordered container
// here, so the result is bit-identical on every platform. Ties always resolve to the lower plate
// index or the earlier cell in row-major order.
//
// Units: one cell is one grid step, (x, y) = (column, row), index = y * width + x. Heights are
// metres relative to the generator's reference sea level 0; consumers apply any axiom
// sea_level_offset_m themselves. The grid does not wrap.
namespace worldforge {

constexpr int kTectonicsMinSize = 16;
constexpr int kTectonicsMaxSize = 1024;
constexpr int kTectonicsMinPlates = 2;
constexpr int kTectonicsMaxPlates = 64;
constexpr int kTectonicsMinHeightM = -6000;
constexpr int kTectonicsMaxHeightM = 8000;

enum class PlateType : std::uint8_t { Continental, Oceanic };

// Describes the pair of plates meeting at a boundary cell, judged from their drift vectors against
// the line joining their seeds: closing fastest head-on is Convergent, pulling apart is Divergent,
// mostly sliding sideways (or no relative drift) is Transform.
enum class BoundaryKind : std::uint8_t { None, Convergent, Divergent, Transform };

struct TectonicPlate {
    int seedX = 0;
    int seedY = 0;
    PlateType type = PlateType::Continental;
    int driftX = 0; // in [-8, 8], arbitrary units; only the ratio between plates matters
    int driftY = 0;
    int baseHeightM = 0;
};

struct TectonicsParams {
    int width = 256;
    int height = 256;
    int plateCount = 12;
};

struct TectonicMap {
    std::uint64_t seed = 0;
    int width = 0;
    int height = 0;
    std::vector<TectonicPlate> plates;
    // Per cell, row-major, each of size width * height:
    std::vector<std::uint8_t> plateId;  // index into plates; every cell has exactly one
    std::vector<std::uint8_t> boundary; // BoundaryKind; non-None iff a 4-neighbour has another plate
    std::vector<std::int16_t> heightM;  // clamped to [kTectonicsMinHeightM, kTectonicsMaxHeightM]

    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); }
    BoundaryKind boundaryAt(int x, int y) const { return static_cast<BoundaryKind>(boundary[index(x, y)]); }
};

namespace detail {

constexpr std::uint64_t kTectonicsDomain = 0x7465637431706c74ull;

enum TectonicsSalt : std::uint32_t { kSaltPosition = 1, kSaltType = 2, kSaltDrift = 3, kSaltBase = 4, kSaltWarpX = 5, kSaltWarpY = 6, kSaltRidge = 7 };

constexpr std::uint64_t tectonicsHash(std::uint64_t seed, std::uint32_t salt, std::uint32_t a)
{
    return splitmix64(splitmix64(seed ^ kTectonicsDomain) ^ hashCell(static_cast<std::int32_t>(salt), static_cast<std::int32_t>(a)));
}

// Value noise on a lattice of `step` cells, bilinear in integers. x and y are non-negative; the
// result is in [0, 1023].
inline std::int64_t latticeNoise(std::uint64_t channelSeed, std::int64_t x, std::int64_t y, std::int64_t step)
{
    const std::int64_t gx = x / step;
    const std::int64_t gy = y / step;
    const std::int64_t fx = x % step;
    const std::int64_t fy = y % step;
    const auto corner = [&](std::int64_t cx, std::int64_t cy) {
        return static_cast<std::int64_t>(splitmix64(channelSeed ^ hashCell(static_cast<std::int32_t>(cx), static_cast<std::int32_t>(cy))) & 1023u);
    };
    const std::int64_t top = corner(gx, gy) * (step - fx) + corner(gx + 1, gy) * fx;
    const std::int64_t bottom = corner(gx, gy + 1) * (step - fx) + corner(gx + 1, gy + 1) * fx;
    return (top * (step - fy) + bottom * fy) / (step * step);
}

constexpr std::int64_t absInt(std::int64_t v) { return v < 0 ? -v : v; }

inline BoundaryKind classifyBoundary(const TectonicPlate& a, const TectonicPlate& b)
{
    const std::int64_t dx = b.seedX - a.seedX;
    const std::int64_t dy = b.seedY - a.seedY;
    const std::int64_t rx = a.driftX - b.driftX;
    const std::int64_t ry = a.driftY - b.driftY;
    const std::int64_t closing = rx * dx + ry * dy;
    const std::int64_t shear = rx * dy - ry * dx;
    if (absInt(closing) > absInt(shear)) {
        return closing > 0 ? BoundaryKind::Convergent : BoundaryKind::Divergent;
    }
    return BoundaryKind::Transform;
}

inline int boundaryRank(BoundaryKind kind)
{
    switch (kind) {
    case BoundaryKind::Convergent: return 3;
    case BoundaryKind::Divergent: return 2;
    case BoundaryKind::Transform: return 1;
    case BoundaryKind::None: break;
    }
    return 0;
}

// Peak displacement in metres at the boundary itself for a cell of plate type `self`, where
// `pairContinental` counts how many of the two plates at the boundary are continental.
inline int boundaryPeakM(BoundaryKind kind, PlateType self, int pairContinental)
{
    switch (kind) {
    case BoundaryKind::Convergent:
        if (pairContinental == 2) return 4500;
        if (pairContinental == 1) return self == PlateType::Continental ? 3000 : -1800;
        return 1500;
    case BoundaryKind::Divergent:
        return self == PlateType::Continental ? -1200 : 900;
    case BoundaryKind::Transform:
    case BoundaryKind::None: break;
    }
    return 0;
}

}

// Returns nullopt when the size or plate count is outside the kTectonics* limits.
// Otherwise a pure function of (worldSeed, params).
inline std::optional<TectonicMap> generateTectonics(std::uint64_t worldSeed, const TectonicsParams& params)
{
    if (params.width < kTectonicsMinSize || params.width > kTectonicsMaxSize || params.height < kTectonicsMinSize || params.height > kTectonicsMaxSize
        || params.plateCount < kTectonicsMinPlates || params.plateCount > kTectonicsMaxPlates) {
        return std::nullopt;
    }

    const int w = params.width;
    const int h = params.height;
    const int count = params.plateCount;
    const std::size_t cells = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const int minSide = std::min(w, h);

    TectonicMap map;
    map.seed = worldSeed;
    map.width = w;
    map.height = h;
    map.plates.resize(static_cast<std::size_t>(count));

    // Distinct seed cells: hash to a start cell, then probe forward to the next free one.
    std::vector<std::uint8_t> taken(cells, 0);
    for (int i = 0; i < count; ++i) {
        const auto ui = static_cast<std::uint32_t>(i);
        std::size_t cell = static_cast<std::size_t>(detail::tectonicsHash(worldSeed, detail::kSaltPosition, ui) % cells);
        while (taken[cell] != 0) cell = (cell + 1) % cells;
        taken[cell] = 1;
        TectonicPlate& plate = map.plates[static_cast<std::size_t>(i)];
        plate.seedX = static_cast<int>(cell % static_cast<std::size_t>(w));
        plate.seedY = static_cast<int>(cell / static_cast<std::size_t>(w));
        plate.type = (i == 0) ? PlateType::Continental : (i == 1) ? PlateType::Oceanic : (detail::tectonicsHash(worldSeed, detail::kSaltType, ui) % 5 < 2 ? PlateType::Continental : PlateType::Oceanic);
        const std::uint64_t drift = detail::tectonicsHash(worldSeed, detail::kSaltDrift, ui);
        plate.driftX = static_cast<int>(drift % 17) - 8;
        plate.driftY = static_cast<int>((drift >> 32) % 17) - 8;
        const std::uint64_t base = detail::tectonicsHash(worldSeed, detail::kSaltBase, ui);
        plate.baseHeightM = plate.type == PlateType::Continental ? 100 + static_cast<int>(base % 400) : -4000 + static_cast<int>(base % 1500);
    }

    // Voronoi with a warped lookup point, so plate edges meander instead of being straight bisectors.
    const std::uint64_t warpX = detail::tectonicsHash(worldSeed, detail::kSaltWarpX, 0);
    const std::uint64_t warpY = detail::tectonicsHash(worldSeed, detail::kSaltWarpY, 0);
    const std::int64_t warpStep = std::max(4, minSide / 8);
    const std::int64_t warpAmp = std::max(2, minSide / 10);
    map.plateId.assign(cells, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::int64_t px = x + (detail::latticeNoise(warpX, x, y, warpStep) - 512) * warpAmp / 512;
            const std::int64_t py = y + (detail::latticeNoise(warpY, x, y, warpStep) - 512) * warpAmp / 512;
            std::int64_t best = -1;
            int bestPlate = 0;
            for (int i = 0; i < count; ++i) {
                const TectonicPlate& plate = map.plates[static_cast<std::size_t>(i)];
                const std::int64_t ddx = px - plate.seedX;
                const std::int64_t ddy = py - plate.seedY;
                const std::int64_t d2 = ddx * ddx + ddy * ddy;
                if (best < 0 || d2 < best) {
                    best = d2;
                    bestPlate = i;
                }
            }
            map.plateId[map.index(x, y)] = static_cast<std::uint8_t>(bestPlate);
        }
    }

    std::vector<std::uint8_t> kindTable(static_cast<std::size_t>(count) * static_cast<std::size_t>(count), 0);
    for (int a = 0; a < count; ++a) {
        for (int b = 0; b < count; ++b) {
            if (a != b) {
                kindTable[static_cast<std::size_t>(a * count + b)] = static_cast<std::uint8_t>(detail::classifyBoundary(map.plates[static_cast<std::size_t>(a)], map.plates[static_cast<std::size_t>(b)]));
            }
        }
    }

    // Boundary cells: on both sides of an edge between two plates. Where several neighbours differ,
    // the strongest kind wins (convergent over divergent over transform), earliest of E, S, W, N on a tie.
    constexpr int kDx[4] = { 1, 0, -1, 0 };
    constexpr int kDy[4] = { 0, 1, 0, -1 };
    map.boundary.assign(cells, 0);
    std::vector<std::uint8_t> otherPlate(cells, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t c = map.index(x, y);
            const int self = map.plateId[c];
            int bestRank = 0;
            for (int n = 0; n < 4; ++n) {
                const int nx = x + kDx[n];
                const int ny = y + kDy[n];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const int other = map.plateId[map.index(nx, ny)];
                if (other == self) continue;
                const auto kind = static_cast<BoundaryKind>(kindTable[static_cast<std::size_t>(self * count + other)]);
                if (detail::boundaryRank(kind) > bestRank) {
                    bestRank = detail::boundaryRank(kind);
                    map.boundary[c] = static_cast<std::uint8_t>(kind);
                    otherPlate[c] = static_cast<std::uint8_t>(other);
                }
            }
        }
    }

    // Distance to the nearest boundary cell (4-connected steps), out to `reach` cells, remembering
    // which boundary cell it came from so ridges can vary along their length.
    const int reach = std::max(3, minSide / 12);
    constexpr std::uint16_t kFar = 0xFFFF;
    std::vector<std::uint16_t> dist(cells, kFar);
    std::vector<std::uint32_t> source(cells, 0);
    std::vector<std::uint32_t> queue;
    for (std::size_t c = 0; c < cells; ++c) {
        if (map.boundary[c] != 0) {
            dist[c] = 0;
            source[c] = static_cast<std::uint32_t>(c);
            queue.push_back(static_cast<std::uint32_t>(c));
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::size_t c = queue[head];
        if (dist[c] + 1 >= reach) continue;
        const int x = static_cast<int>(c % static_cast<std::size_t>(w));
        const int y = static_cast<int>(c / static_cast<std::size_t>(w));
        for (int n = 0; n < 4; ++n) {
            const int nx = x + kDx[n];
            const int ny = y + kDy[n];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const std::size_t nc = map.index(nx, ny);
            if (dist[nc] != kFar) continue;
            dist[nc] = static_cast<std::uint16_t>(dist[c] + 1);
            source[nc] = source[c];
            queue.push_back(static_cast<std::uint32_t>(nc));
        }
    }

    const std::uint64_t ridgeSeed = detail::tectonicsHash(worldSeed, detail::kSaltRidge, 0);
    const std::int64_t ridgeStep = std::max(4, minSide / 16);
    const std::int64_t reach2 = static_cast<std::int64_t>(reach) * reach;
    map.heightM.assign(cells, 0);
    for (std::size_t c = 0; c < cells; ++c) {
        const TectonicPlate& plate = map.plates[map.plateId[c]];
        std::int64_t height = plate.baseHeightM;
        if (dist[c] != kFar) {
            const std::size_t s = source[c];
            const int sx = static_cast<int>(s % static_cast<std::size_t>(w));
            const int sy = static_cast<int>(s / static_cast<std::size_t>(w));
            const BoundaryKind kind = static_cast<BoundaryKind>(map.boundary[s]);
            const int pairContinental = (map.plates[map.plateId[s]].type == PlateType::Continental ? 1 : 0)
                + (map.plates[otherPlate[s]].type == PlateType::Continental ? 1 : 0);
            const std::int64_t peak = detail::boundaryPeakM(kind, plate.type, pairContinental);
            const std::int64_t along = 640 + (detail::latticeNoise(ridgeSeed, sx, sy, ridgeStep) * 384 >> 10);
            const std::int64_t t = reach - dist[c];
            height += peak * along / 1024 * (t * t) / reach2;
        }
        map.heightM[c] = static_cast<std::int16_t>(std::clamp<std::int64_t>(height, kTectonicsMinHeightM, kTectonicsMaxHeightM));
    }
    return map;
}

// Order-sensitive digest of every field of the map; the golden value that guards determinism.
inline std::uint64_t hashTectonicMap(const TectonicMap& map)
{
    std::uint64_t h = splitmix64(map.seed ^ detail::kTectonicsDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    for (const TectonicPlate& p : map.plates) {
        mix(static_cast<std::uint64_t>(p.seedX));
        mix(static_cast<std::uint64_t>(p.seedY));
        mix(static_cast<std::uint64_t>(p.type));
        mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(p.driftX)));
        mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(p.driftY)));
        mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(p.baseHeightM)));
    }
    for (std::size_t c = 0; c < map.plateId.size(); ++c) {
        mix(static_cast<std::uint64_t>(map.plateId[c]) | static_cast<std::uint64_t>(map.boundary[c]) << 8 | static_cast<std::uint64_t>(static_cast<std::uint16_t>(map.heightM[c])) << 16);
    }
    return h;
}

}
