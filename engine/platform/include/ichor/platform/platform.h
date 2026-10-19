#pragma once

#include "ichor/core/input.h"

#include <string>

namespace ichor {

struct WindowDesc {
    std::string title = "Ichor";
    int width = 1280;
    int height = 720;
    bool hidden = false; // created never shown: nothing appears on the desktop
};

struct Extent {
    int width = 0;
    int height = 0;
};

// The mouse as of the last pump, in pixels of the drawable, origin top left.
struct Pointer {
    int x = 0;
    int y = 0;
    bool primary = false; // the left button is down
    bool inside = false;  // the window has the mouse
};

// The arrow and page keys as of the last pump. The editor reads them; the simulation never does.
struct NavKeys {
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool pageUp = false;
    bool pageDown = false;
    bool shift = false;
};

class Platform {
public:
    virtual ~Platform() = default;

    virtual bool init(const WindowDesc& desc) = 0;
    virtual void pumpEvents() = 0;
    virtual bool quitRequested() const = 0;
    virtual void requestQuit() = 0;
    // Whether Esc ends the run outright. The app turns it off once it has a menu for Esc to open.
    virtual void setEscapeQuits(bool quits) { (void)quits; }
    virtual double now() const = 0;
    virtual Extent drawableSize() const = 0;

    // The player's input as of the last pump, in simulation terms.
    virtual InputFrame input() const = 0;

    // For the UI only: the simulation never reads it. Platforms without a mouse report none.
    virtual Pointer pointer() const { return {}; }
    virtual NavKeys navKeys() const { return {}; }
    // While set, the arrow keys are not the player's movement (the editor nudges with them).
    virtual void reserveArrowKeys(bool reserved) { (void)reserved; }
    // While set, the mouse (captured while the right button is held) and the left and right arrows turn the player (InputFrame::lookX) instead of moving.
    virtual void enableLook(bool enabled) { (void)enabled; }
    // Scales how far the mouse turns the player; 1 is the default. Only meaningful with enableLook.
    virtual void setMouseSensitivity(float sensitivity) { (void)sensitivity; }
};

}
