#pragma once

#include <functional>

namespace ichor {

// Outdoor reverb from terrain (docs/12-audio-atmosphere.md, "Reverb in spaces nobody designed"):
// a fixed fan of rays is marched across a height field around the listener, and cliff walls and
// canyon sides that rise above ear level count as reflectors. Pure arithmetic with a bounded
// cost (kOutdoorReverbRays * kOutdoorReverbSteps height samples) and no libm, so it never feeds
// the world hash and agrees on every platform.

// World-space height at (x, z) in metres. May return NaN or inf for missing terrain; those
// samples are treated as open air, never as a wall.
using OutdoorHeightFn = std::function<float(float x, float z)>;

constexpr int kOutdoorReverbRays = 8;
constexpr int kOutdoorReverbSteps = 16;
constexpr float kOutdoorReverbRange = 80.0f;    // metres, ray length
constexpr float kOutdoorSpeedOfSound = 343.0f;  // m/s
constexpr float kOutdoorMaxWet = 0.6f;
constexpr float kOutdoorMinDecaySeconds = 0.2f;
constexpr float kOutdoorMaxDecaySeconds = 4.0f;

struct OutdoorReverbParams {
    // Fraction 0..1 of rays that reach full range without hitting terrain. 1 on an open plain.
    float openness = 1.0f;
    // Distance in metres to the nearest reflector, kOutdoorReverbRange when there is none.
    float nearestReflector = kOutdoorReverbRange;
    // Reverb tail length in seconds.
    float decaySeconds = kOutdoorMinDecaySeconds;
    // Wet level 0..kOutdoorMaxWet. Exactly 0 when nothing reflects.
    float wet = 0.0f;
    // Delay of the first echo in milliseconds (round trip to the nearest reflector).
    float earlyDelayMs = 2.0f * kOutdoorReverbRange / kOutdoorSpeedOfSound * 1000.0f;
};

// The listener's ear height is earY (world metres). Terrain taller than earY at a sample counts
// as a reflector. A non-finite listener position or earY, or an empty heightAt, yields the open
// plain defaults. Heights are clamped to a sane band before comparison. Deterministic.
OutdoorReverbParams estimateOutdoorReverb(const OutdoorHeightFn& heightAt, float x, float z,
                                          float earY);

}  // namespace ichor
