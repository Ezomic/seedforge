#pragma once

#include "ichor/assets/bundle.h"

namespace ichor {

// Rain below this is too light to hear, so the ambience stays silent through the first drizzle.
constexpr float kRainDeadZone = 0.05f;
constexpr float kRainMaxGain = 0.6f;

// How loud the rain bed is for a rain amount: silent in the dead zone, then an ease-in and
// ease-out curve up to kRainMaxGain. NaN is silence; anything outside 0..1 is clamped.
float rainAmbienceGain(float rain);

// The bed itself, synthesised: hissing filtered noise with a slow swell, built so the last sample
// runs straight into the first and it can loop for as long as it rains.
SoundClip makeRainClip();

}
