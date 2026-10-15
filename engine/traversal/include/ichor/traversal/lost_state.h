#pragma once

#include "ichor/traversal/lost_visibility.h"

namespace ichor {

// Getting lost follows from low visibility with no landmark in sight and no compass. Disorientation is integer
// micro-units so the state hashes identically everywhere.
constexpr int kMaxDisorientation = 1000000;
constexpr int kLostThreshold = 700000;
constexpr int kFoundThreshold = 300000;
// Per second, at zero visibility; scaled down to nothing as visibility reaches kLostVisibilityMeters.
constexpr int kGainPerSecond = 100000;
// Per second with a landmark, a compass or clear sight.
constexpr int kSlowRecoverPerSecond = 50000;
// Per second on a road, beside a river or on high ground. Always available, so lost is never a dead end.
constexpr int kGuideRecoverPerSecond = 400000;
constexpr int kMaxStepMs = 5000;

struct LostState {
    int disorientation = 0;
    // Set at kLostThreshold and cleared only at or below kFoundThreshold.
    bool lost = false;
};

struct LostSenses {
    bool landmarkInSight = false;
    bool hasCompass = false;
    // On a road, beside a river (follow it downhill) or on high ground.
    bool onGuide = false;
};

// Advances the state by dtMs milliseconds (clamped to 0..kMaxStepMs) at a visibility in metres. Any input is safe.
LostState stepLostState(LostState state, int visibilityMeters, const LostSenses& senses, int dtMs);

}
