#include "ichor/traversal/traversal_sizing.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr std::uint64_t kUnit = 1000000000000ULL; // fixed-point numerator for seconds per metre
constexpr std::uint32_t kMinSpeedMm = 100;
constexpr std::uint32_t kMaxSpeedMm = 100000;
constexpr std::uint32_t kInvalid = 0xFFFFFFFFU;

// 0 for an absent speed, kInvalid for NaN, infinite or negative.
std::uint32_t toMm(float metresPerSecond)
{
    if (!std::isfinite(metresPerSecond) || metresPerSecond < 0.0F) {
        return kInvalid;
    }
    if (metresPerSecond == 0.0F) {
        return 0;
    }
    const float clamped = std::min(metresPerSecond, static_cast<float>(kMaxSpeedMm / 1000));
    const auto mm = static_cast<std::uint32_t>(clamped * 1000.0F);
    return std::clamp(mm, kMinSpeedMm, kMaxSpeedMm);
}

std::uint32_t scaleMetres(std::uint32_t speedMm, std::uint32_t seconds)
{
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(speedMm) * seconds / 1000);
}

}

std::uint32_t travelSeconds(std::uint32_t metres, std::uint32_t speedMmPerS)
{
    if (speedMmPerS == 0) {
        return 0;
    }
    const std::uint64_t seconds = static_cast<std::uint64_t>(metres) * 1000 / speedMmPerS;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(seconds, 0xFFFFFFFFU));
}

WorldSizing sizeWorld(const TraversalSet& set)
{
    WorldSizing out;

    std::uint32_t foot = toMm(set.footSpeed);
    std::uint32_t mount = toMm(set.mountSpeed);
    std::uint32_t boat = toMm(set.boatSpeed);
    if (foot == kInvalid || foot == 0 || mount == kInvalid || boat == kInvalid) {
        out.status = SizingStatus::InvalidSpeed;
    }
    if (foot == kInvalid || foot == 0) {
        foot = toMm(kSizingDefaultFootSpeed);
    }
    if (mount == kInvalid) {
        mount = 0;
    }
    if (boat == kInvalid) {
        boat = 0;
    }

    const std::uint64_t mountShare = mount == 0 ? 0 : std::min<std::uint64_t>(set.mountedShare, 1000);
    const std::uint64_t boatShare = boat == 0 ? 0 : std::min<std::uint64_t>(set.boatShare, 1000 - mountShare);
    const std::uint64_t footShare = 1000 - mountShare - boatShare;

    std::uint64_t weighted = footShare * (kUnit / foot);
    if (mount != 0) {
        weighted += mountShare * (kUnit / mount);
    }
    if (boat != 0) {
        weighted += boatShare * (kUnit / boat);
    }
    const std::uint64_t speed = kUnit * 1000 / weighted;
    out.effectiveSpeedMmPerS = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(speed, kMinSpeedMm, kMaxSpeedMm));

    std::uint64_t edge = scaleMetres(out.effectiveSpeedMmPerS, kSizingWorldSeconds);
    if (set.fastTravel) {
        edge = edge * kSizingFastTravelStretchPermille / 1000;
    }
    const std::uint64_t clamped = std::clamp<std::uint64_t>(edge, kSizingMinEdgeMetres, kSizingMaxEdgeMetres);
    out.edgeClamped = clamped != edge;
    out.worldEdgeMetres = static_cast<std::uint32_t>(clamped);

    out.regionSpanMetres = std::max<std::uint32_t>(1, scaleMetres(out.effectiveSpeedMmPerS, kSizingRegionSeconds));
    out.regionSpanMetres = std::min(out.regionSpanMetres, out.worldEdgeMetres);
    out.regionsPerEdge = std::max<std::uint32_t>(1, (out.worldEdgeMetres + out.regionSpanMetres / 2) / out.regionSpanMetres);
    out.siteSpacingMetres = std::max<std::uint32_t>(1, scaleMetres(out.effectiveSpeedMmPerS, kSizingSiteSeconds));
    out.glanceSpacingMetres = std::max<std::uint32_t>(1, scaleMetres(out.effectiveSpeedMmPerS, kSizingGlanceSeconds));
    return out;
}

bool sizingWithinBounds(const WorldSizing& sizing)
{
    return sizing.effectiveSpeedMmPerS != 0
        && travelSeconds(sizing.siteSpacingMetres, sizing.effectiveSpeedMmPerS) <= kSizingSiteMaxSeconds
        && travelSeconds(sizing.regionSpanMetres, sizing.effectiveSpeedMmPerS) <= kSizingRegionMaxSeconds;
}

}
