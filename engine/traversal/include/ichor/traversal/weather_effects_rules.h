#pragma once

#include "ichor/traversal/lost_visibility.h"

namespace ichor {

// Doc 12: weather is mechanical, not cosmetic. These are the multipliers weather puts on other systems. Callers apply
// them to their own numbers (walk speed, grip, hull stability, stamina cost, torch burn time); this file owns none of
// those systems. Every field is finite. The scale fields are in (0, 1] and only ever shrink, the drain field is at
// least 1 and only ever grows.
struct WeatherEffects {
    float moveSpeedScale = 1.0f;
    float climbGripScale = 1.0f;
    float boatStabilityScale = 1.0f;
    float staminaDrainScale = 1.0f;
    // Fraction of the normal chance that a carried flame stays lit. 1 is dry and calm.
    float fireSurvivalScale = 1.0f;
};

// Intensity 0 gives the unmodified effects for any kind. Intensity moves linearly to the kind's worst case at 1, so
// worse weather never helps: every field is monotone in intensity, and Storm is at least as bad as Rain and Fog on
// every field. An intensity that is not a number counts as 0, outside [0, 1] is clamped. An unknown kind counts as
// clear. Uses only add and multiply, so the result is the same on every platform.
WeatherEffects weatherEffects(const LostWeatherState& weather);

}
