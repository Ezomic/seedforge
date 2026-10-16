#pragma once

namespace ichor {

// Gains of the time-of-day ambience layers of docs/12-audio-atmosphere.md. Pure function of the
// world clock hour, so it holds no sim state and never feeds the world hash. Every gain is in
// 0..1 and the four never sum above 1.
struct AmbienceTimeOfDay {
    float dawnBirds = 0.0f;
    float dayInsects = 0.0f;
    float dusk = 0.0f;
    float night = 0.0f;
};

constexpr float kAmbienceHoursPerDay = 24.0f;
// Hour used when the clock reading is NaN, infinite or too large to wrap exactly: broad daylight.
constexpr float kAmbienceFallbackHour = 12.0f;

// Maps any reading onto [0, 24). Negative hours wrap backwards, NaN and inf become the fallback.
float ambienceWrapHour(float hour);

// Smooth crossfades (smoothstep edges) between the layers. Night fades out before the dawn chorus
// rises, so the pre-dawn stretch is a deliberate near-silence where the total dips toward 0. The
// other hand-overs are complementary, so the total stays at 1. Continuous across every boundary
// including midnight, identical on every platform (no libm).
AmbienceTimeOfDay ambienceTimeOfDay(float hour);

float ambienceTimeOfDayTotal(const AmbienceTimeOfDay& g);

}  // namespace ichor
