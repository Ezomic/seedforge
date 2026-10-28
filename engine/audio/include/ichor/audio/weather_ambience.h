#pragma once

#include "ichor/assets/bundle_types.h"
#include "ichor/audio/rain_ambience.h"

namespace ichor {

// Wind below this blend of overcast and rain is too faint to hear, so clear weather stays quiet.
constexpr float kWindDeadZone = 0.1f;
constexpr float kWindMaxGain = 0.4f;

// How loud the wind bed is for the sky: overcast carries most of it, rain adds the rest. Silent in
// the dead zone, then an ease-in and ease-out curve up to kWindMaxGain. NaN in either input is
// silence; anything outside 0..1 is clamped.
float windAmbienceGain(float overcast, float rain);

// The gains the weather asks of its two beds. Rain plus wind never exceed kAmbienceBudget, so the
// layer budget never has to rescale them against each other.
struct WeatherAudioTargets {
    float wind = 0.0f;
    float rain = 0.0f;
};
WeatherAudioTargets weatherAudioTargets(float overcast, float rain);

// The wind bed, synthesised: low rumbling noise with slow gusts, built so the last sample runs
// straight into the first and it can loop for as long as the sky stays grey.
SoundClip makeWindClip();

}
