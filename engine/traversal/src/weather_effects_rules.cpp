#include "ichor/traversal/weather_effects_rules.h"

namespace ichor {

namespace {

constexpr WeatherEffects kClear{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
constexpr WeatherEffects kRainWorst{0.9f, 0.6f, 0.85f, 1.1f, 0.5f};
constexpr WeatherEffects kFogWorst{0.85f, 0.9f, 0.9f, 1.05f, 0.9f};
constexpr WeatherEffects kStormWorst{0.6f, 0.3f, 0.4f, 1.5f, 0.15f};

float cleanIntensity(float intensity)
{
    if (!(intensity > 0.0f)) {
        return 0.0f;
    }
    return intensity < 1.0f ? intensity : 1.0f;
}

const WeatherEffects& worstCase(LostWeather kind)
{
    switch (kind) {
    case LostWeather::Rain:
        return kRainWorst;
    case LostWeather::Fog:
        return kFogWorst;
    case LostWeather::Storm:
        return kStormWorst;
    case LostWeather::Clear:
        break;
    }
    return kClear;
}

float lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

}

WeatherEffects weatherEffects(const LostWeatherState& weather)
{
    const float t = cleanIntensity(weather.intensity);
    const WeatherEffects& worst = worstCase(weather.kind);
    return {
        lerp(kClear.moveSpeedScale, worst.moveSpeedScale, t),
        lerp(kClear.climbGripScale, worst.climbGripScale, t),
        lerp(kClear.boatStabilityScale, worst.boatStabilityScale, t),
        lerp(kClear.staminaDrainScale, worst.staminaDrainScale, t),
        lerp(kClear.fireSurvivalScale, worst.fireSurvivalScale, t),
    };
}

}
