#pragma once

#include "ichor/core/input.h"

#include <string>

namespace ichor {

struct WindowDesc {
    std::string title = "Ichor";
    int width = 1280;
    int height = 720;
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

class Platform {
public:
    virtual ~Platform() = default;

    virtual bool init(const WindowDesc& desc) = 0;
    virtual void pumpEvents() = 0;
    virtual bool quitRequested() const = 0;
    virtual double now() const = 0;
    virtual Extent drawableSize() const = 0;

    // The player's input as of the last pump, in simulation terms.
    virtual InputFrame input() const = 0;

    // For the UI only: the simulation never reads it. Platforms without a mouse report none.
    virtual Pointer pointer() const { return {}; }
};

}
