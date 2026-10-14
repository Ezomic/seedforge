#pragma once

#include "ichor/editor/picking.h"

#include <glm/vec3.hpp>

#include <optional>

namespace ichor {

inline constexpr double kNudgeStep = 0.5;
inline constexpr double kNudgeFastStep = 5.0;

// The nudge keys that went down this frame; `fast` is the Shift state.
struct NudgeKeys {
    bool left = false;
    bool right = false;
    bool forward = false;
    bool back = false;
    bool up = false;
    bool down = false;
    bool fast = false;
};

// The world offset one frame of nudge presses asks for. Left, right, forward and back go
// along the camera's right and forward axes flattened onto the ground, so left is screen
// left; up and down go along Y. The ground move is one step long however many of the four
// are down (a diagonal is not faster) and opposite keys cancel. Looking straight up or
// down, where forward has no ground direction, the camera's up axis stands in for it.
// Nothing when no key is down or the camera is not made of numbers.
std::optional<glm::dvec3> nudgeOffset(const NudgeKeys& keys, const PickCamera& camera);

}
