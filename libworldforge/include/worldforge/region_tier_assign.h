#pragma once

#include "subseed.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Forge-time region tiers (doc 04, Pacing without a fixed path). Difficulty is a property of place:
// each region gets a tier from how far it is from the start, how dangerous its inputs are, and the
// world seed. Only arithmetic and comparisons, no libm transcendentals, so results match on every platform.
//
// Tier boundaries are concentric rings whose widths the seed jitters once per boundary, not per
// region. That keeps the result monotone in distance across the whole world: with danger held equal,
// a region further from the start never gets a lower tier than a nearer one.
namespace worldforge {

constexpr std::uint32_t kMaxRegionTiers = 255;

struct RegionTierParams {
    std::uint32_t tierCount = 5;
    double ringWidth = 100.0;   // nominal distance covered by one tier; finite, > 0
    double ringJitter = 0.3;    // each ring's width varies by up to +/- this fraction; [0, 1)
    double dangerWeight = 0.5;  // danger 1 makes a region count as (1 + dangerWeight) times further; finite, >= 0
};

struct RegionTierInput {
    double distance = 0.0;  // travel distance from the start; NaN and +inf count as unreachable (top tier)
    double danger = 0.0;    // 0..1, clamped; NaN counts as 0
};

namespace detail {

constexpr std::uint64_t kTierDomain = 0x7469725f72696e67ull;

inline bool validRegionTierParams(const RegionTierParams& p)
{
    return p.tierCount >= 1 && p.tierCount <= kMaxRegionTiers
        && std::isfinite(p.ringWidth) && p.ringWidth > 0.0
        && p.ringJitter >= 0.0 && p.ringJitter < 1.0
        && std::isfinite(p.dangerWeight) && p.dangerWeight >= 0.0;
}

inline double clampDanger(double danger)
{
    if (!(danger > 0.0)) return 0.0;
    return danger > 1.0 ? 1.0 : danger;
}

}

// Distances at which tier 1, 2, ... begin: tierCount - 1 strictly increasing values. nullopt when
// the parameters are invalid.
inline std::optional<std::vector<double>> regionTierBoundaries(std::uint64_t worldSeed, const RegionTierParams& p)
{
    if (!detail::validRegionTierParams(p)) return std::nullopt;
    std::vector<double> edges;
    edges.reserve(p.tierCount - 1);
    const std::uint64_t base = splitmix64(worldSeed ^ detail::kTierDomain);
    double edge = 0.0;
    for (std::uint32_t k = 1; k < p.tierCount; ++k) {
        const std::uint64_t h = splitmix64(base ^ splitmix64(k));
        // Top 53 bits mapped to [-1, 1): exact in a double, so identical everywhere.
        const double unit = static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0;
        edge += p.ringWidth * (1.0 + p.ringJitter * unit);
        edges.push_back(edge);
    }
    return edges;
}

// One tier per input, in input order, each a pure function of its own input, the seed and the
// parameters. nullopt when the parameters are invalid.
inline std::optional<std::vector<std::uint8_t>> assignRegionTiers(
    std::uint64_t worldSeed, const RegionTierParams& p, std::span<const RegionTierInput> regions)
{
    const auto edges = regionTierBoundaries(worldSeed, p);
    if (!edges) return std::nullopt;
    const auto top = static_cast<std::uint8_t>(p.tierCount - 1);

    std::vector<std::uint8_t> tiers;
    tiers.reserve(regions.size());
    for (const RegionTierInput& r : regions) {
        if (std::isnan(r.distance)) {
            tiers.push_back(top);
            continue;
        }
        const double distance = r.distance > 0.0 ? r.distance : 0.0;
        const double effective = distance * (1.0 + p.dangerWeight * detail::clampDanger(r.danger));
        const auto it = std::upper_bound(edges->begin(), edges->end(), effective);
        tiers.push_back(static_cast<std::uint8_t>(it - edges->begin()));
    }
    return tiers;
}

}
