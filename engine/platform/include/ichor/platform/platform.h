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
};

}
