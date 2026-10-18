#pragma once

#include "ichor/stage0/camera_view.h"

namespace ichor::stage0 {

enum class CameraFraming {
    Follow, // over the player's back, aimed at the midpoint of the two fighters
    LockOn, // over the shoulder, pulled back as the foe gets further away, aimed at the foe side
};

struct CameraTuning {
    CameraFraming framing = CameraFraming::Follow;
    float distance = 3.2f;     // metres behind the player
    float height = 2.0f;       // metres above the player
    float wallClearance = 0.4f; // the eye never gets closer than this to an arena wall
    float minBoom = 0.8f;      // the eye never gets closer than this to the player horizontally, unless a wall forces it
    float smoothing = 8.0f;    // 1 / seconds; how fast the camera swings round when the fighters swap sides
};

// The pose the camera aims for with the boom pointing along `away` (a horizontal direction from the foe through
// the player). It is a pure function of its inputs, so the killing-blow telemetry and the render agree on it.
// A non-finite input yields the default pose.
CameraPose frameCamera(glm::dvec3 player, glm::dvec3 foe, glm::dvec3 away, const CameraTuning& tuning);

// The horizontal unit direction from the foe through the player, or +Z when they coincide.
glm::dvec3 awayFrom(glm::dvec3 player, glm::dvec3 foe);

// Steps the boom direction toward `target` by a rate that does not depend on libm. The result is not normalised:
// keeping it raw lets two opposite directions pass through zero instead of stalling, so callers normalise to use it.
glm::dvec3 smoothAway(glm::dvec3 current, glm::dvec3 target, double dt, float rate);

// The stateful render camera. Positions come in already blended; the rig only smooths the swing around the player.
class CameraRig {
public:
    explicit CameraRig(CameraTuning tuning = {}) : tuning_(tuning) {}

    CameraPose update(glm::dvec3 player, glm::dvec3 foe, double dt);

    const CameraTuning& tuning() const { return tuning_; }

private:
    CameraTuning tuning_;
    glm::dvec3 away_{0.0, 0.0, 1.0};
    CameraPose pose_{};
    bool primed_ = false;
};

}
