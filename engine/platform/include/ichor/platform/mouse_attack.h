#pragma once

#include "ichor/core/input.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ichor {

// Turns mouse buttons into combat input. The left button is a light attack, Ctrl+left a heavy one, and the middle
// button blocks while held; the right button stays mouse look. A click is held for kPulseSeconds so a frame that
// runs no simulation tick cannot lose it; the combat state refuses a second attack while one is committed, so a
// click is still one swing. While disabled (no --combat, or a UI screen has focus) clicks are ignored outright.
class MouseAttack {
public:
    static constexpr float kPulseSeconds = 0.05f;

    void setEnabled(bool enabled)
    {
        if (!enabled) {
            pulseLeft_ = 0.0f;
            block_ = false;
        }
        enabled_ = enabled;
    }
    bool enabled() const { return enabled_; }

    void click(bool heavy)
    {
        if (!enabled_) return;
        heavy_ = heavy;
        pulseLeft_ = kPulseSeconds;
    }

    void setBlockHeld(bool held) { block_ = enabled_ && held; }

    // The button bits for this pump, and the pulse runs down by elapsed.
    std::uint32_t step(float elapsed)
    {
        std::uint32_t bits = 0;
        if (pulseLeft_ > 0.0f) {
            bits |= heavy_ ? InputFrame::Heavy : InputFrame::Attack;
            pulseLeft_ = std::isfinite(elapsed) && elapsed > 0.0f ? std::max(0.0f, pulseLeft_ - elapsed) : pulseLeft_;
        }
        if (block_) bits |= InputFrame::Block;
        return bits;
    }

private:
    bool enabled_ = false;
    bool heavy_ = false;
    bool block_ = false;
    float pulseLeft_ = 0.0f;
};

}
