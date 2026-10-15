#include "ichor/traversal/diving.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace ichor {

namespace {

constexpr std::array<GearProfile, 5> kProfiles = {{
    {2.0f, 30000},
    {12.0f, 60000},
    {40.0f, 300000},
    {120.0f, 900000},
    {400.0f, 1800000},
}};

constexpr std::array<DiveGear, 5> kGearOrder = {
    DiveGear::Bare, DiveGear::Breath, DiveGear::Helmet, DiveGear::PressureSuit, DiveGear::Orichalcum,
};

float cleanDepth(float depth)
{
    if (!(depth > 0.0f)) {
        return 0.0f;
    }
    return std::min(depth, kMaxDepthMetres);
}

std::int64_t cleanDwellMs(float seconds)
{
    if (!(seconds > 0.0f)) {
        return 0;
    }
    return static_cast<std::int64_t>(std::min(seconds, kMaxDwellSeconds)) * 1000;
}

}

GearProfile gearProfile(DiveGear gear)
{
    const auto index = static_cast<std::size_t>(gear);
    return index < kProfiles.size() ? kProfiles[index] : kProfiles[0];
}

DepthCheck checkDepth(float depth, DiveGear gear, const CapabilitySet& capabilities)
{
    const float clean = cleanDepth(depth);
    if (clean > gearProfile(gear).maxDepth) {
        return DepthCheck::TooDeep;
    }
    if (clean > kProfiles[0].maxDepth && !capabilities.has(Capability::Dive)) {
        return DepthCheck::NeedsDive;
    }
    return DepthCheck::Reachable;
}

float deepestReachable(DiveGear gear, const CapabilitySet& capabilities)
{
    const float gearDepth = gearProfile(gear).maxDepth;
    return capabilities.has(Capability::Dive) ? gearDepth : std::min(gearDepth, kProfiles[0].maxDepth);
}

std::optional<DiveGear> minimumGear(float depth)
{
    const float clean = cleanDepth(depth);
    for (const DiveGear gear : kGearOrder) {
        if (clean <= gearProfile(gear).maxDepth) {
            return gear;
        }
    }
    return std::nullopt;
}

int airDrainPermille(float depth)
{
    const float clean = cleanDepth(depth);
    if (clean <= 0.0f) {
        return 0;
    }
    const int decimetres = static_cast<int>(clean * 10.0f);
    return 1000 + 10 * decimetres;
}

int airRemainingMs(DiveGear gear, float depth, float dwellSeconds)
{
    const int air = gearProfile(gear).airMs;
    const std::int64_t spent = cleanDwellMs(dwellSeconds) * airDrainPermille(depth) / 1000;
    return spent >= air ? 0 : air - static_cast<int>(spent);
}

int secondsOfAir(DiveGear gear, float depth)
{
    const int air = gearProfile(gear).airMs;
    const int drain = airDrainPermille(depth);
    if (drain == 0) {
        return air / 1000;
    }
    return static_cast<int>(static_cast<std::int64_t>(air) * 1000 / drain / 1000);
}

}
