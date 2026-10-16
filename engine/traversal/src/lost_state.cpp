#include "ichor/traversal/lost_state.h"

#include <algorithm>
#include <cstdint>

namespace ichor {

LostState stepLostState(LostState state, int visibilityMeters, const LostSenses& senses, int dtMs)
{
    const std::int64_t dt = std::clamp(dtMs, 0, kMaxStepMs);
    const std::int64_t visibility = std::clamp(visibilityMeters, 0, kLostVisibilityMeters);
    std::int64_t value = std::clamp(state.disorientation, 0, kMaxDisorientation);

    const bool sighted = senses.landmarkInSight || senses.hasCompass || visibility >= kLostVisibilityMeters;
    if (senses.onGuide) {
        value -= kGuideRecoverPerSecond * dt / 1000;
    } else if (sighted) {
        value -= kSlowRecoverPerSecond * dt / 1000;
    } else {
        const std::int64_t pressure = (kLostVisibilityMeters - visibility) * 1000 / kLostVisibilityMeters;
        value += pressure * kGainPerSecond * dt / 1000000;
    }

    value = std::clamp<std::int64_t>(value, 0, kMaxDisorientation);
    state.disorientation = static_cast<int>(value);
    if (state.disorientation >= kLostThreshold) {
        state.lost = true;
    } else if (state.disorientation <= kFoundThreshold) {
        state.lost = false;
    }
    return state;
}

}
