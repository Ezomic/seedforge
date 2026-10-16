#include "ichor/audio/ambience_pressure.h"

namespace ichor {

namespace {

float clampLevel(float level)
{
    if (!(level > 0.0f)) return 0.0f;
    if (level > 1.0f) return 1.0f;
    return level;
}

// 0 at or below lo, 1 at or above hi, linear between.
float ramp(float x, float lo, float hi)
{
    if (x <= lo) return 0.0f;
    if (x >= hi) return 1.0f;
    return (x - lo) / (hi - lo);
}

constexpr float kLivingGoneAt = 0.6f;
constexpr float kDroneEntersAt = 0.15f;
constexpr float kDropoutFullAt = 0.8f;

}  // namespace

AmbiencePressureParams ambiencePressure(float level)
{
    const float p = clampLevel(level);
    AmbiencePressureParams out;
    out.livingGain = 1.0f - ramp(p, 0.0f, kLivingGoneAt);
    out.droneGain = ramp(p, kDroneEntersAt, 1.0f);
    out.lowpassHz = kAmbienceCalmLowpassHz + (kAmbienceFullLowpassHz - kAmbienceCalmLowpassHz) * p;
    out.detuneCents = kAmbienceMaxDetuneCents * p;
    out.livingDropout = ramp(p, 0.0f, kDropoutFullAt);
    return out;
}

bool livingOneShotSurvives(float level, float roll)
{
    const float r = clampLevel(roll);
    return r >= ambiencePressure(level).livingDropout;
}

}  // namespace ichor
