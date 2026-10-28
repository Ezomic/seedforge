#pragma once

#include "ichor/core/input.h"

#include <cstdint>

namespace ichor {

// Keeps the F8 note box's keyboard away from the game. While typing, the player's input is empty; when the box
// closes, a key still down (the Enter that saved, the Esc that cancelled) stays swallowed until it is let go, so
// it neither retries on the death screen nor opens the menu.
class TypingGuard {
public:
    void setTyping(bool typing, const InputFrame& raw)
    {
        typing_ = typing;
        held_ = typing ? InputFrame{} : raw;
    }
    bool typing() const { return typing_; }

    InputFrame filter(const InputFrame& raw)
    {
        if (typing_) {
            return {};
        }
        held_.buttons &= raw.buttons;
        InputFrame out = raw;
        out.buttons &= ~held_.buttons;
        hold(held_.moveX, raw.moveX, out.moveX);
        hold(held_.moveY, raw.moveY, out.moveY);
        hold(held_.lookX, raw.lookX, out.lookX);
        return out;
    }

private:
    // An axis held at close stays at rest until it comes back to rest itself.
    static void hold(float& held, float raw, float& out)
    {
        if (held != 0.0f && raw != 0.0f) {
            out = 0.0f;
        } else {
            held = 0.0f;
        }
    }

    bool typing_ = false;
    InputFrame held_;
};

}
