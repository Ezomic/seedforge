#include "ichor/traversal/lost_visibility.h"

#include "ichor/core/dmath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ichor {

namespace {

constexpr float kMaxHour = 1.0e6f;
constexpr int kFullPermille = 1000;
constexpr int kNightLightPermille = 150;
// Daylight ramps up across this band of the sun's height (cosine of its angle from midday).
constexpr float kTwilightLow = -0.2f;
constexpr float kTwilightHigh = 0.2f;

int worstWeatherPermille(LostWeather kind)
{
    switch (kind) {
    case LostWeather::Rain:
        return 400;
    case LostWeather::Fog:
        return 40;
    case LostWeather::Storm:
        return 120;
    default:
        return kFullPermille;
    }
}

int weatherPermille(const LostWeatherState& weather)
{
    const float raw = weather.intensity;
    const float intensity = std::isfinite(raw) ? std::clamp(raw, 0.0f, 1.0f) : 0.0f;
    const int intensityPermille = static_cast<int>(intensity * 1000.0f + 0.5f);
    const int worst = worstWeatherPermille(weather.kind);
    return kFullPermille - (kFullPermille - worst) * intensityPermille / 1000;
}

int lightPermille(float hourOfDay)
{
    float hour = hourOfDay;
    if (!std::isfinite(hour) || std::fabs(hour) > kMaxHour) {
        hour = 12.0f;
    }
    const float sun = dmath::cos((hour - 12.0f) * (dmath::kTwoPi / 24.0f));
    const float day = std::clamp((sun - kTwilightLow) / (kTwilightHigh - kTwilightLow), 0.0f, 1.0f);
    const int dayPermille = static_cast<int>(day * 1000.0f + 0.5f);
    return kNightLightPermille + (kFullPermille - kNightLightPermille) * dayPermille / 1000;
}

}

int visibilityRangeMeters(const LostWeatherState& weather, float hourOfDay)
{
    const std::int64_t metres = static_cast<std::int64_t>(kMaxVisibilityMeters) * weatherPermille(weather)
        * lightPermille(hourOfDay) / (static_cast<std::int64_t>(kFullPermille) * kFullPermille);
    return static_cast<int>(std::clamp<std::int64_t>(metres, kMinVisibilityMeters, kMaxVisibilityMeters));
}

}
