#pragma once

#include "ichor/assets/bundle.h"

#include <optional>

namespace ichor {

struct SplashCue {
    float gain = 1.0f; // 0..1, louder in deeper water
};

// Decides when the player's wading makes a splash. It is fed the player's place and the water
// depth under them once per fixed tick and counts the ground covered in water, so the pace
// follows the speed and standing still is silent. It only listens: nothing here is part of the
// simulation's state or its hash.
class WadingSplash {
public:
    static constexpr double kStrideMetres = 1.1;
    // No walker covers this in one tick, so a bigger step is a teleport or a load, not a stride.
    static constexpr double kMaxStepMetres = 5.0;
    static constexpr float kQuietGain = 0.25f;

    std::optional<SplashCue> step(double x, double z, float depth);

private:
    bool placed_ = false;
    double x_ = 0.0;
    double z_ = 0.0;
    double sinceSplash_ = 0.0;
};

// The splash itself, synthesised: a burst of filtered noise with a falling bubble under it.
SoundClip makeSplashClip();

}
