#include "ichor/audio/ambience_tod.h"

#include <cstdint>

namespace ichor {

namespace {

constexpr float kMaxExactHour = 1.0e6f;

float smooth(float t)
{
    if (!(t > 0.0f)) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

float ramp(float h, float from, float to)
{
    return smooth((h - from) / (to - from));
}

// 0 below a, rising to 1 at b, holding to c, falling to 0 at d. d may exceed 24 to cross midnight.
float window(float h, float a, float b, float c, float d)
{
    return ramp(h, a, b) * (1.0f - ramp(h, c, d));
}

float wrapped(float h, float a, float b, float c, float d)
{
    const float direct = window(h, a, b, c, d);
    const float shifted = window(h + kAmbienceHoursPerDay, a, b, c, d);
    return direct > shifted ? direct : shifted;
}

}  // namespace

float ambienceWrapHour(float hour)
{
    if (!(hour > -kMaxExactHour && hour < kMaxExactHour)) return kAmbienceFallbackHour;
    const auto whole = static_cast<std::int64_t>(hour);
    float floorHour = static_cast<float>(whole);
    if (floorHour > hour) floorHour -= 1.0f;
    const auto days = static_cast<std::int64_t>(floorHour / kAmbienceHoursPerDay);
    float h = hour - static_cast<float>(days) * kAmbienceHoursPerDay;
    while (h < 0.0f) h += kAmbienceHoursPerDay;
    while (h >= kAmbienceHoursPerDay) h -= kAmbienceHoursPerDay;
    return h;
}

AmbienceTimeOfDay ambienceTimeOfDay(float hour)
{
    const float h = ambienceWrapHour(hour);
    AmbienceTimeOfDay g;
    g.dawnBirds = wrapped(h, 5.0f, 6.0f, 7.0f, 9.0f);
    g.dayInsects = wrapped(h, 7.0f, 9.0f, 17.0f, 19.0f);
    g.dusk = wrapped(h, 17.0f, 19.0f, 20.0f, 22.0f);
    g.night = wrapped(h, 20.0f, 22.0f, 27.5f, 28.5f);
    return g;
}

float ambienceTimeOfDayTotal(const AmbienceTimeOfDay& g)
{
    return g.dawnBirds + g.dayInsects + g.dusk + g.night;
}

}  // namespace ichor
