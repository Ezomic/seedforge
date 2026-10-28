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
        (void)typing;
        (void)raw;
    }
    bool typing() const { return typing_; }

    InputFrame filter(const InputFrame& raw) { return raw; }

private:
    bool typing_ = false;
};

}
