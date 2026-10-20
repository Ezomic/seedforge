#pragma once

#include <algorithm>
#include <cmath>

namespace ichor {

// Turns relative mouse motion into the look axis (InputFrame::lookX). The mouse only turns the camera while the
// right button is held, so the cursor stays free for the UI otherwise. The axis is a turn speed, so the turn per
// frame does not depend on the frame or tick rate, and it is what the replay stream records.
class MouseLook {
public:
    // Mouse speed that counts as a full-rate turn at sensitivity 1, in window units per second.
    static constexpr float kFullTurnMouseSpeed = 1500.0f;
    static constexpr float kSmoothingSeconds = 0.05f;

    void setHeld(bool held)
    {
        if (!held) {
            pending_ = 0.0f;
            speed_ = 0.0f;
        }
        held_ = held;
    }
    bool held() const { return held_; }

    void setSensitivity(float sensitivity) { sensitivity_ = std::isfinite(sensitivity) ? std::clamp(sensitivity, 0.05f, 20.0f) : 1.0f; }
    float sensitivity() const { return sensitivity_; }

    void addMotion(float xrel)
    {
        if (held_ && std::isfinite(xrel)) pending_ += xrel;
    }

    // Smoothed over a few pumps so ticks that skip a pump lose nothing. Returns the axis, not yet clamped to the unit range.
    float step(double elapsed)
    {
        const float delta = pending_;
        pending_ = 0.0f;
        if (!held_ || !(elapsed > 1e-4 && elapsed < 0.5)) {
            speed_ = 0.0f;
            return 0.0f;
        }
        const float blend = 1.0f - std::exp(-static_cast<float>(elapsed) / kSmoothingSeconds);
        speed_ += (delta / static_cast<float>(elapsed) - speed_) * blend;
        return speed_ * sensitivity_ / kFullTurnMouseSpeed;
    }

private:
    bool held_ = false;
    float sensitivity_ = 1.0f;
    float pending_ = 0.0f;
    float speed_ = 0.0f;
};

}
