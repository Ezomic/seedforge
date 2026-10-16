#pragma once

#include "ichor/traversal/verbs.h"

#include <cstdint>

namespace ichor {

// Boats on water. Pure rules only: which water a hull can cross, how fast, who may board and where it may land.
// Everything is a function of its arguments, so the result is the same on every platform. A depth, current, wave
// height, wind or slope that is not finite, or a negative current, wave height or wind, is Invalid: a hostile
// reading never lets a boat through. A negative depth reads as zero (dry).

struct BoatSpec {
    float draftMetres = 0.4f;           // the water the hull needs under it
    float maxCurrentMetresPerSecond = 2.0f;
    float maxWaveMetres = 1.0f;
    float maxWindMetresPerSecond = 14.0f;
    int baseSpeedPermille = 1500;       // thousandths of walking pace in still, calm water
    int seats = 4;                      // people, the pilot included
    int cargoKilograms = 200;
};

constexpr int kMaxBoatSpeedPermille = 4000;
// The largest draft, current, wave or wind limit a spec may state, so no limit can overflow a conversion.
constexpr float kMaxBoatLimitMetres = 100.0f;
constexpr int kMaxBoatSeats = 64;
constexpr int kMaxBoatCargoKilograms = 100000;

// How much a current adds to or takes from the hull speed, per metre per second along the heading.
constexpr int kCurrentPermillePerMetrePerSecond = 250;
// Speed lost to waves and wind at the hull's limit; below the limit the loss scales in linearly.
constexpr int kWeatherMaxLossPermille = 500;
// Speed lost with a full load; an empty hull loses none.
constexpr int kLoadMaxLossPermille = 300;
// The most water a person boards or lands through (chest height, the same as DeepWater).
constexpr float kBoardingDepthMetres = 1.2f;
// The fastest a hull may be moving to take on or put off a person.
constexpr float kMooringSpeedMetresPerSecond = 0.5f;

enum class CrossVerdict : std::uint8_t {
    Ok,
    Invalid,       // a reading or the spec is not a usable number
    NoCapability,  // the player has not earned Boat
    TooShallow,    // less water than the draft
    CurrentTooStrong,
    TooRough,      // waves or wind past what the hull takes
    NoHeadway,     // the current and weather leave no forward speed along the heading
};

struct WaterReading {
    float depthMetres = 0.0f;
    float currentMetresPerSecond = 0.0f;
    // Cosine of the angle between the heading and the flow: 1 straight downstream, -1 straight upstream.
    float currentAlignment = 0.0f;
};

struct WeatherReading {
    float waveMetres = 0.0f;
    float windMetresPerSecond = 0.0f;
};

struct Load {
    int people = 1;
    int cargoKilograms = 0;
};

struct CrossResult {
    CrossVerdict verdict = CrossVerdict::Invalid;
    int speedPermille = 0; // thousandths of walking pace; 0 unless verdict is Ok
};

// True when the spec's fields are finite, in range and positive where they must be.
bool boatSpecValid(const BoatSpec& spec);

// Thousandths of the hull's speed left after weather: 1000 in calm, 1000 - kWeatherMaxLossPermille at the limits.
// The worse of waves and wind counts. Zero for unusable input.
int weatherSpeedPermille(const BoatSpec& spec, const WeatherReading& weather);

// Thousandths of the hull's speed left after loading: 1000 empty, 1000 - kLoadMaxLossPermille full. Zero when
// the load does not fit or is negative.
int loadSpeedPermille(const BoatSpec& spec, const Load& load);

// Whether the boat can be steered through this water in this weather with this load, and how fast. Speed is
// base * weather * load, plus the current along the heading, clamped to kMaxBoatSpeedPermille.
CrossResult crossWater(const BoatSpec& spec, const CapabilitySet& capabilities, const WaterReading& water,
                       const WeatherReading& weather, const Load& load);

enum class BoardVerdict : std::uint8_t {
    Ok,
    Invalid,
    NoCapability,
    NoSeat,        // the boat is full or the cargo does not fit
    TooDeep,       // deeper than a person can wade to the boat
    TooFast,       // the boat is moving too fast to step aboard
};

// People and cargo join the load already aboard, from a shore, a dock or the shallows. waterDepthMetres is at the
// boarding point.
BoardVerdict canBoard(const BoatSpec& spec, const CapabilitySet& capabilities, const Load& aboard, const Load& joining,
                      float waterDepthMetres, float boatSpeedMetresPerSecond);

enum class LandVerdict : std::uint8_t {
    Ok,
    Invalid,
    TooDeep,       // no footing within wading depth
    TooSteep,      // the bank is too steep to step onto
    TooFast,
};

// A person steps off at a bank. No capability is needed to get off a boat.
LandVerdict canLand(float waterDepthMetres, float slopeDegrees, float boatSpeedMetresPerSecond);

}
