#pragma once

namespace ichor {

// Doc 11 and doc 12: fog, night and storms genuinely reduce visibility. This is only the range, in whole metres.
// The range comes from weather and time of day; terrain occlusion of landmarks is the caller's concern.
constexpr int kMaxVisibilityMeters = 2000;
constexpr int kMinVisibilityMeters = 8;
// Below this range the walker cannot read the land and may get lost (see lost_state.h).
constexpr int kLostVisibilityMeters = 120;

enum class LostWeather { Clear, Rain, Fog, Storm };

struct LostWeatherState {
    LostWeather kind = LostWeather::Clear;
    // 0 (barely there) to 1 (at its worst). Not a number counts as 0, outside the range is clamped.
    float intensity = 0.0f;
};

// Visibility range in metres, always between kMinVisibilityMeters and kMaxVisibilityMeters. hourOfDay is local
// solar time in hours and wraps around the day; midday is brightest and midnight leaves a little moonlight.
// An hour that is not a number or is absurdly large counts as midday; an unknown weather kind counts as clear.
// Uses only dmath, so the result is the same on every platform.
int visibilityRangeMeters(const LostWeatherState& weather, float hourOfDay);

}
