#include "ichor/audio/outdoor_reverb_estimate.h"

#include <cmath>

namespace ichor {

namespace {

constexpr float kHeightBand = 100000.0f;
constexpr float kStep = kOutdoorReverbRange / kOutdoorReverbSteps;

struct Dir {
    float x;
    float z;
};

constexpr Dir kDirs[kOutdoorReverbRays] = {
    {1.0f, 0.0f},
    {0.70710678f, 0.70710678f},
    {0.0f, 1.0f},
    {-0.70710678f, 0.70710678f},
    {-1.0f, 0.0f},
    {-0.70710678f, -0.70710678f},
    {0.0f, -1.0f},
    {0.70710678f, -0.70710678f},
};

float clampBand(float v)
{
    return v > kHeightBand ? kHeightBand : (v < -kHeightBand ? -kHeightBand : v);
}

float earlyDelayMs(float nearest)
{
    return 2.0f * nearest / kOutdoorSpeedOfSound * 1000.0f;
}

// Distance of the first sample taller than the ear, or a negative number when the ray escapes.
float marchRay(const OutdoorHeightFn& heightAt, Dir dir, float x, float z, float earY)
{
    for (int i = 1; i <= kOutdoorReverbSteps; ++i) {
        const float d = kStep * static_cast<float>(i);
        const float h = heightAt(x + dir.x * d, z + dir.z * d);
        if (!std::isfinite(h)) continue;
        if (clampBand(h) > earY) return d;
    }
    return -1.0f;
}

}  // namespace

OutdoorReverbParams estimateOutdoorReverb(const OutdoorHeightFn& heightAt, float x, float z,
                                          float earY)
{
    OutdoorReverbParams out;
    if (!heightAt || !std::isfinite(x) || !std::isfinite(z) || !std::isfinite(earY)) return out;
    const float ear = clampBand(earY);

    int open = 0;
    float nearest = kOutdoorReverbRange;
    float enclosure = 0.0f;
    for (const Dir& dir : kDirs) {
        const float d = marchRay(heightAt, dir, x, z, ear);
        if (d < 0.0f) {
            ++open;
            continue;
        }
        if (d < nearest) nearest = d;
        enclosure += 1.0f - (d - kStep) / kOutdoorReverbRange;
    }
    enclosure /= static_cast<float>(kOutdoorReverbRays);

    out.openness = static_cast<float>(open) / static_cast<float>(kOutdoorReverbRays);
    out.nearestReflector = nearest;
    out.wet = kOutdoorMaxWet * enclosure;
    out.decaySeconds =
        kOutdoorMinDecaySeconds + (kOutdoorMaxDecaySeconds - kOutdoorMinDecaySeconds) * enclosure;
    out.earlyDelayMs = earlyDelayMs(nearest);
    return out;
}

}  // namespace ichor
