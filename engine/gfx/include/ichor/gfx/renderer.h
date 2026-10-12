#pragma once

#include <string>

namespace ichor {

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool init() = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;

    // One line on the debug overlay for this frame. The null renderer drops it.
    virtual void debugText(std::string line) = 0;
};

}
