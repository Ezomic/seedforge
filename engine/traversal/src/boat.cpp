#include "ichor/traversal/boat.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

bool usable(float value)
{
    return std::isfinite(value) && value >= 0.0f;
}

bool limitValid(float value)
{
    return std::isfinite(value) && value > 0.0f && value <= kMaxBoatLimitMetres;
}

bool loadValid(const Load& load)
{
    return load.people >= 0 && load.people <= kMaxBoatSeats && load.cargoKilograms >= 0
        && load.cargoKilograms <= kMaxBoatCargoKilograms;
}

bool fits(const BoatSpec& spec, const Load& load)
{
    return load.people <= spec.seats && load.cargoKilograms <= spec.cargoKilograms;
}

// Parts per thousand of value over limit, 0 to 1000. The caller has checked value and limit are usable.
int fractionPermille(float value, float limit)
{
    if (value >= limit) {
        return 1000;
    }
    return static_cast<int>(value / limit * 1000.0f);
}

}

bool boatSpecValid(const BoatSpec& spec)
{
    return limitValid(spec.draftMetres) && limitValid(spec.maxCurrentMetresPerSecond)
        && limitValid(spec.maxWaveMetres) && limitValid(spec.maxWindMetresPerSecond)
        && spec.baseSpeedPermille > 0 && spec.baseSpeedPermille <= kMaxBoatSpeedPermille
        && spec.seats > 0 && spec.seats <= kMaxBoatSeats
        && spec.cargoKilograms >= 0 && spec.cargoKilograms <= kMaxBoatCargoKilograms;
}

int weatherSpeedPermille(const BoatSpec& spec, const WeatherReading& weather)
{
    if (!boatSpecValid(spec) || !usable(weather.waveMetres) || !usable(weather.windMetresPerSecond)) {
        return 0;
    }
    const int worst = std::max(fractionPermille(weather.waveMetres, spec.maxWaveMetres),
                               fractionPermille(weather.windMetresPerSecond, spec.maxWindMetresPerSecond));
    return 1000 - worst * kWeatherMaxLossPermille / 1000;
}

int loadSpeedPermille(const BoatSpec& spec, const Load& load)
{
    if (!boatSpecValid(spec) || !loadValid(load) || !fits(spec, load)) {
        return 0;
    }
    const int bySeats = load.people * 1000 / spec.seats;
    const int byCargo = spec.cargoKilograms > 0 ? load.cargoKilograms * 1000 / spec.cargoKilograms : 0;
    return 1000 - std::max(bySeats, byCargo) * kLoadMaxLossPermille / 1000;
}

CrossResult crossWater(const BoatSpec& spec, const CapabilitySet& capabilities, const WaterReading& water,
                       const WeatherReading& weather, const Load& load)
{
    CrossResult result;
    if (!boatSpecValid(spec) || !loadValid(load) || !fits(spec, load) || !std::isfinite(water.depthMetres)
        || !usable(water.currentMetresPerSecond) || !std::isfinite(water.currentAlignment)
        || !usable(weather.waveMetres) || !usable(weather.windMetresPerSecond)) {
        return result;
    }
    if (!capabilities.has(Capability::Boat)) {
        result.verdict = CrossVerdict::NoCapability;
        return result;
    }
    if (water.depthMetres < spec.draftMetres) {
        result.verdict = CrossVerdict::TooShallow;
        return result;
    }
    if (water.currentMetresPerSecond > spec.maxCurrentMetresPerSecond) {
        result.verdict = CrossVerdict::CurrentTooStrong;
        return result;
    }
    if (weather.waveMetres > spec.maxWaveMetres || weather.windMetresPerSecond > spec.maxWindMetresPerSecond) {
        result.verdict = CrossVerdict::TooRough;
        return result;
    }
    const float alignment = std::clamp(water.currentAlignment, -1.0f, 1.0f);
    const int current = static_cast<int>(water.currentMetresPerSecond * alignment * kCurrentPermillePerMetrePerSecond);
    const int hull = spec.baseSpeedPermille * weatherSpeedPermille(spec, weather) / 1000
        * loadSpeedPermille(spec, load) / 1000;
    const int speed = std::min(hull + current, kMaxBoatSpeedPermille);
    if (speed <= 0) {
        result.verdict = CrossVerdict::NoHeadway;
        return result;
    }
    result.verdict = CrossVerdict::Ok;
    result.speedPermille = speed;
    return result;
}

BoardVerdict canBoard(const BoatSpec& spec, const CapabilitySet& capabilities, const Load& aboard, const Load& joining,
                      float waterDepthMetres, float boatSpeedMetresPerSecond)
{
    if (!boatSpecValid(spec) || !loadValid(aboard) || !loadValid(joining) || !std::isfinite(waterDepthMetres)
        || !usable(boatSpeedMetresPerSecond)) {
        return BoardVerdict::Invalid;
    }
    if (!capabilities.has(Capability::Boat)) {
        return BoardVerdict::NoCapability;
    }
    const Load total{aboard.people + joining.people, aboard.cargoKilograms + joining.cargoKilograms};
    if (total.people > spec.seats || total.cargoKilograms > spec.cargoKilograms) {
        return BoardVerdict::NoSeat;
    }
    if (std::max(waterDepthMetres, 0.0f) > kBoardingDepthMetres) {
        return BoardVerdict::TooDeep;
    }
    if (boatSpeedMetresPerSecond > kMooringSpeedMetresPerSecond) {
        return BoardVerdict::TooFast;
    }
    return BoardVerdict::Ok;
}

LandVerdict canLand(float waterDepthMetres, float slopeDegrees, float boatSpeedMetresPerSecond)
{
    if (!std::isfinite(waterDepthMetres) || !usable(slopeDegrees) || !usable(boatSpeedMetresPerSecond)) {
        return LandVerdict::Invalid;
    }
    if (std::max(waterDepthMetres, 0.0f) > kBoardingDepthMetres) {
        return LandVerdict::TooDeep;
    }
    if (slopeDegrees >= kSteepSlopeDegrees) {
        return LandVerdict::TooSteep;
    }
    if (boatSpeedMetresPerSecond > kMooringSpeedMetresPerSecond) {
        return LandVerdict::TooFast;
    }
    return LandVerdict::Ok;
}

}
