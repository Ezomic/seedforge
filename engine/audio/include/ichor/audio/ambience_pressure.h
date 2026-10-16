#pragma once

namespace ichor {

// What the Pressure does to the ambience of docs/12-audio-atmosphere.md: birds thin then vanish,
// a low drone enters and rises, the one-shot pool loses its living entries. Pure functions of one
// number, so they hold no sim state and never feed the world hash.
struct AmbiencePressureParams {
    // Gain multiplier for the living layers (birds and the like). 1 at calm, exactly 0 once gone.
    float livingGain = 1.0f;
    // Gain of the low drone layer. 0 at calm, 1 at full pressure.
    float droneGain = 0.0f;
    // Low-pass cutoff for the whole ambience bus, in Hz. Falls as the pressure rises.
    float lowpassHz = 0.0f;
    // Pitch drift of the beds in cents (always >= 0, applied as a flat detune).
    float detuneCents = 0.0f;
    // Chance 0..1 that a living one-shot is dropped from the pool.
    float livingDropout = 0.0f;
};

constexpr float kAmbienceCalmLowpassHz = 20000.0f;
constexpr float kAmbienceFullLowpassHz = 800.0f;
constexpr float kAmbienceMaxDetuneCents = 50.0f;

// Pressure arrives as a normalised level that rises over time: 0 is calm, 1 is the front on top of
// the player. Anything outside 0..1 is clamped, and NaN or negative counts as calm (a bad reading
// must never make the world louder or quieter than it was authored). Every output is monotonic in
// the level, continuous, and identical on every platform (no libm).
AmbiencePressureParams ambiencePressure(float level);

// Whether a living one-shot survives. roll is a deterministic draw in [0, 1); NaN or out of range
// is clamped (NaN counts as 0, so it survives only at zero dropout). Never more likely to survive
// at a higher level for the same roll.
bool livingOneShotSurvives(float level, float roll);

}  // namespace ichor
