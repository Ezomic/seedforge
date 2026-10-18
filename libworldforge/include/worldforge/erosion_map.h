#pragma once

#include <worldforge/subseed.h>
#include <worldforge/tectonics_map.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Phase 2, Erosion (doc 08): hydraulic and thermal erosion of a TectonicMap's height field into a
// new, eroded field. Phases 3 to 6 consume ErodedMap; none of them may mutate it. The input
// TectonicMap is never modified; the plate and boundary fields stay valid for the same grid and
// are read from the TectonicMap, not copied here.
//
// Output contract: ErodedMap::heightM is the final terrain elevation in metres, relative to the
// same reference sea level 0 as TectonicMap::heightM, same grid (width, height, row-major,
// index = y * width + x), clamped to [kTectonicsMinHeightM, kTectonicsMaxHeightM]. Hydrology
// (phase 3) should treat it as the terrain before any river channels are carved.
//
// Algorithm: a fixed number of rounds, each a hydraulic pass followed by a thermal pass.
//  - Hydraulic: D8 steepest-descent receivers, flow accumulation from a per-cell rain amount drawn
//    from the subseed hash, then a sediment-capacity model walking cells from high to low: cells
//    below capacity erode (never below half the drop to the receiver, so slopes keep their sign),
//    cells above capacity deposit. Sinks and ocean cells deposit everything they receive.
//  - Thermal: every cell sheds material to lower neighbours whose slope exceeds the talus angle,
//    computed from a snapshot of the field so cell order cannot matter.
//
// Determinism: integer fixed-point only (heights held in 1/16 m), no libm, no floating point and
// no unordered container; ordering is by (height, index) keys, so the result is bit-identical on
// every platform. Randomness comes only from subseed.h hashing. Memory is O(cells), at most about
// 40 bytes per cell, with cells <= kTectonicsMaxSize^2.
namespace worldforge {

constexpr int kErosionMaxIterations = 256;
constexpr int kErosionMaxRate = 256;
constexpr int kErosionMaxTalusM = 1000;
// Cap on cell visits (cells * iterations * (1 + thermalPasses)), so one call cannot run for hours.
constexpr std::int64_t kErosionMaxCellPasses = 1'000'000'000;

struct ErosionParams {
    int iterations = 24;       // rounds of hydraulic then thermal, in [0, kErosionMaxIterations]
    int thermalPasses = 2;     // thermal passes per round, in [0, kErosionMaxIterations]
    int strength = 128;        // sediment capacity scale per unit of flux times drop, in [0, 256]
    int erodeRate = 160;       // share (of 256) of spare capacity converted to erosion per pass
    int depositRate = 128;     // share (of 256) of excess sediment deposited per pass
    int thermalRate = 128;     // share (of 256) of the steepest excess moved per thermal pass
    int talusM = 5;            // largest stable height step between orthogonal neighbours, [0, 1000]
};

struct ErodedMap {
    std::uint64_t seed = 0; // TectonicMap::seed it was derived from
    int width = 0;
    int height = 0;
    std::vector<std::int16_t> heightM; // eroded elevation, size width * height

    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); }
};

namespace detail {

constexpr std::uint64_t kErosionDomain = 0x65726f73696f6e32ull;
constexpr std::int32_t kErosionFixed = 16; // heights are held in 1/16 m
constexpr std::int64_t kErosionMinH16 = static_cast<std::int64_t>(kTectonicsMinHeightM) * kErosionFixed;
constexpr std::int64_t kErosionMaxH16 = static_cast<std::int64_t>(kTectonicsMaxHeightM) * kErosionFixed;

inline std::uint64_t isqrtU64(std::uint64_t v)
{
    std::uint64_t result = 0;
    std::uint64_t bit = std::uint64_t{ 1 } << 62;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= result + bit) {
            v -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

constexpr int kErosionDx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
constexpr int kErosionDy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
constexpr bool erosionDiagonal(int n) { return (n & 1) != 0; }

struct ErosionField {
    int w = 0;
    int h = 0;
    std::vector<std::int32_t> z; // elevation in 1/16 m
};

inline void hydraulicPass(ErosionField& f, const ErosionParams& p, std::uint64_t rainSeed, std::uint32_t iteration)
{
    const std::size_t cells = f.z.size();
    constexpr std::uint32_t kNoReceiver = 0xFFFFFFFFu;
    std::vector<std::uint32_t> receiver(cells, kNoReceiver);
    std::vector<std::int32_t> drop(cells, 0);
    std::vector<std::uint64_t> keys(cells);
    std::vector<std::uint32_t> area(cells);
    std::vector<std::int64_t> sediment(cells, 0);

    for (int y = 0; y < f.h; ++y) {
        for (int x = 0; x < f.w; ++x) {
            const std::size_t c = static_cast<std::size_t>(y) * static_cast<std::size_t>(f.w) + static_cast<std::size_t>(x);
            std::int64_t bestScaled = 0;
            for (int n = 0; n < 8; ++n) {
                const int nx = x + kErosionDx[n];
                const int ny = y + kErosionDy[n];
                if (nx < 0 || ny < 0 || nx >= f.w || ny >= f.h) continue;
                const std::size_t nc = static_cast<std::size_t>(ny) * static_cast<std::size_t>(f.w) + static_cast<std::size_t>(nx);
                const std::int64_t d = static_cast<std::int64_t>(f.z[c]) - f.z[nc];
                if (d <= 0) continue;
                const std::int64_t scaled = d * (erosionDiagonal(n) ? 181 : 256);
                if (scaled > bestScaled) {
                    bestScaled = scaled;
                    receiver[c] = static_cast<std::uint32_t>(nc);
                    drop[c] = static_cast<std::int32_t>(d);
                }
            }
            keys[c] = (static_cast<std::uint64_t>(static_cast<std::int64_t>(f.z[c]) - kErosionMinH16) << 32) | static_cast<std::uint64_t>(c);
            area[c] = 16u + static_cast<std::uint32_t>(splitmix64(rainSeed ^ hashCell(static_cast<std::int32_t>(iteration), static_cast<std::int32_t>(c))) & 15u);
        }
    }
    std::sort(keys.begin(), keys.end(), [](std::uint64_t a, std::uint64_t b) { return a > b; });

    for (const std::uint64_t key : keys) {
        const std::size_t c = static_cast<std::size_t>(key & 0xFFFFFFFFu);
        const std::int64_t incoming = sediment[c];
        const std::uint32_t rec = receiver[c];
        if (rec == kNoReceiver || f.z[c] < 0) {
            f.z[c] = static_cast<std::int32_t>(std::clamp<std::int64_t>(f.z[c] + incoming, kErosionMinH16, kErosionMaxH16));
            continue;
        }
        area[rec] += area[c];
        const std::int64_t flux = static_cast<std::int64_t>(isqrtU64(area[c]));
        const std::int64_t capacity = flux * drop[c] * p.strength >> 12;
        std::int64_t outgoing = incoming;
        if (incoming > capacity) {
            const std::int64_t deposit = (incoming - capacity) * p.depositRate >> 8;
            f.z[c] = static_cast<std::int32_t>(std::clamp<std::int64_t>(f.z[c] + deposit, kErosionMinH16, kErosionMaxH16));
            outgoing -= deposit;
        } else {
            const std::int64_t erode = std::min<std::int64_t>((capacity - incoming) * p.erodeRate >> 8, drop[c] / 2);
            f.z[c] = static_cast<std::int32_t>(f.z[c] - erode);
            outgoing += erode;
        }
        sediment[rec] += outgoing;
    }
}

inline void thermalPass(ErosionField& f, const ErosionParams& p)
{
    const std::size_t cells = f.z.size();
    const std::int64_t talusOrtho = static_cast<std::int64_t>(p.talusM) * kErosionFixed;
    const std::int64_t talusDiag = talusOrtho * 362 / 256;
    std::vector<std::int32_t> delta(cells, 0);

    for (int y = 0; y < f.h; ++y) {
        for (int x = 0; x < f.w; ++x) {
            const std::size_t c = static_cast<std::size_t>(y) * static_cast<std::size_t>(f.w) + static_cast<std::size_t>(x);
            std::int64_t excess[8] = {};
            std::int64_t total = 0;
            std::int64_t largest = 0;
            for (int n = 0; n < 8; ++n) {
                const int nx = x + kErosionDx[n];
                const int ny = y + kErosionDy[n];
                if (nx < 0 || ny < 0 || nx >= f.w || ny >= f.h) continue;
                const std::size_t nc = static_cast<std::size_t>(ny) * static_cast<std::size_t>(f.w) + static_cast<std::size_t>(nx);
                const std::int64_t e = static_cast<std::int64_t>(f.z[c]) - f.z[nc] - (erosionDiagonal(n) ? talusDiag : talusOrtho);
                if (e <= 0) continue;
                excess[n] = e;
                total += e;
                largest = std::max(largest, e);
            }
            if (total == 0) continue;
            const std::int64_t moved = (largest * p.thermalRate >> 8) / 2;
            if (moved == 0) continue;
            std::int64_t given = 0;
            for (int n = 0; n < 8; ++n) {
                if (excess[n] == 0) continue;
                const std::int64_t share = moved * excess[n] / total;
                const std::size_t nc = static_cast<std::size_t>(y + kErosionDy[n]) * static_cast<std::size_t>(f.w) + static_cast<std::size_t>(x + kErosionDx[n]);
                delta[nc] += static_cast<std::int32_t>(share);
                given += share;
            }
            delta[c] -= static_cast<std::int32_t>(given);
        }
    }
    for (std::size_t c = 0; c < cells; ++c) {
        f.z[c] = static_cast<std::int32_t>(std::clamp<std::int64_t>(static_cast<std::int64_t>(f.z[c]) + delta[c], kErosionMinH16, kErosionMaxH16));
    }
}

inline bool erosionInputValid(const TectonicMap& map)
{
    if (map.width < kTectonicsMinSize || map.width > kTectonicsMaxSize || map.height < kTectonicsMinSize || map.height > kTectonicsMaxSize) return false;
    if (map.heightM.size() != static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height)) return false;
    return std::all_of(map.heightM.begin(), map.heightM.end(), [](std::int16_t v) { return v >= kTectonicsMinHeightM && v <= kTectonicsMaxHeightM; });
}

inline bool erosionParamsValid(const ErosionParams& p)
{
    const auto inRange = [](int v, int hi) { return v >= 0 && v <= hi; };
    return inRange(p.iterations, kErosionMaxIterations) && inRange(p.thermalPasses, kErosionMaxIterations) && inRange(p.strength, kErosionMaxRate)
        && inRange(p.erodeRate, kErosionMaxRate) && inRange(p.depositRate, kErosionMaxRate) && inRange(p.thermalRate, kErosionMaxRate)
        && inRange(p.talusM, kErosionMaxTalusM);
}

}

// Returns nullopt when the map is malformed (size outside the kTectonics* limits, a height field
// of the wrong length or with a height outside the allowed range), a parameter is out of range, or
// the total work exceeds kErosionMaxCellPasses.
// Otherwise a pure function of (map.seed, map.width, map.height, map.heightM, params). With zero
// iterations the result equals the input heights.
inline std::optional<ErodedMap> erodeTectonics(const TectonicMap& map, const ErosionParams& params = {})
{
    if (!detail::erosionInputValid(map) || !detail::erosionParamsValid(params)) return std::nullopt;
    const std::int64_t passes = static_cast<std::int64_t>(params.iterations) * (1 + params.thermalPasses);
    if (passes * static_cast<std::int64_t>(map.heightM.size()) > kErosionMaxCellPasses) return std::nullopt;

    detail::ErosionField field;
    field.w = map.width;
    field.h = map.height;
    field.z.resize(map.heightM.size());
    for (std::size_t c = 0; c < map.heightM.size(); ++c) field.z[c] = static_cast<std::int32_t>(map.heightM[c]) * detail::kErosionFixed;

    const std::uint64_t rainSeed = splitmix64(map.seed ^ detail::kErosionDomain);
    for (int it = 0; it < params.iterations; ++it) {
        detail::hydraulicPass(field, params, rainSeed, static_cast<std::uint32_t>(it));
        for (int t = 0; t < params.thermalPasses; ++t) detail::thermalPass(field, params);
    }

    ErodedMap out;
    out.seed = map.seed;
    out.width = map.width;
    out.height = map.height;
    out.heightM.resize(field.z.size());
    for (std::size_t c = 0; c < field.z.size(); ++c) {
        const std::int32_t m = (field.z[c] + detail::kErosionFixed / 2) >> 4;
        out.heightM[c] = static_cast<std::int16_t>(std::clamp(m, kTectonicsMinHeightM, kTectonicsMaxHeightM));
    }
    return out;
}

// Order-sensitive digest of every field of the map; the golden value that guards determinism.
inline std::uint64_t hashErodedMap(const ErodedMap& map)
{
    std::uint64_t h = splitmix64(map.seed ^ detail::kErosionDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    for (const std::int16_t v : map.heightM) mix(static_cast<std::uint64_t>(static_cast<std::uint16_t>(v)));
    return h;
}

}
