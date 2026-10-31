#pragma once

#include "ichor/core/input.h"

#include <string>
#include <vector>

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

    bool operator==(const Extent&) const = default;
};

// What the player asks of the window from the pause menu. A 0 x 0 size is the window as it was made: the default
// window size, or the display's own mode in fullscreen.
struct DisplayRequest {
    bool fullscreen = false;
    Extent size;
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
    // Whether F3, the debug overlay key, is down as of the last pump.
    virtual bool overlayKey() const { return false; }
    // Whether the window had keyboard focus as of the last pump. Platforms without a window always do.
    virtual bool focused() const { return true; }
    // Whether F1, the controls card key, is down as of the last pump.
    virtual bool helpKey() const { return false; }
    // Whether F2, the sound legend key, and F4, the captions key, are down as of the last pump.
    virtual bool soundLegendKey() const { return false; }
    virtual bool captionKey() const { return false; }
    // Whether F8, the feedback note key, is down as of the last pump.
    virtual bool noteKey() const { return false; }
    // While on, the keyboard types into the F8 note box and input() is empty; a key still down when it goes off stays
    // out of input() until it is let go.
    virtual void setTextEntry(bool on) { (void)on; }
    // What was typed since the last call, in order: UTF-8 text, '\b' for Backspace, '\r' for Enter, '\x1b' for Esc.
    virtual std::string takeTypedText() { return {}; }
    // While set, the arrow keys are not the player's movement (the editor nudges with them).
    virtual void reserveArrowKeys(bool reserved) { (void)reserved; }
    // While set, the mouse (captured while the right button is held) and the left and right arrows turn the player (InputFrame::lookX) instead of moving.
    virtual void enableLook(bool enabled) { (void)enabled; }
    // Scales how far the mouse turns the player; 1 is the default. Only meaningful with enableLook.
    virtual void setMouseSensitivity(float sensitivity) { (void)sensitivity; }
    // While set, the left button attacks (Ctrl for heavy) and the middle button blocks, as InputFrame::Attack, Heavy
    // and Block. The app turns it off whenever a UI screen has focus, so a click on the UI stays a UI click.
    virtual void enableMouseAttack(bool enabled) { (void)enabled; }
    // The sizes the window's display offers for fullscreen, largest first and each once. Platforms without a display offer none.
    virtual std::vector<Extent> displayModes() const { return {}; }
    // Puts the window in the asked mode and size; false when there is no window or the display refused. Fullscreen at
    // a size no mode has uses the closest mode; a window is kept inside the usable screen.
    virtual bool setDisplay(const DisplayRequest& request) { (void)request; return false; }
    virtual bool fullscreen() const { return false; }
};

}
