#pragma once

#include "ichor/gfx/renderer.h"

#include <cstdint>

namespace ichor {

class NullRenderer final : public Renderer {
public:
    bool init() override { return true; }
    void beginFrame() override;
    void endFrame() override;
    void debugText(std::string line) override;

    std::uint64_t framesBegun() const { return begun_; }
    std::uint64_t framesEnded() const { return ended_; }
    std::uint64_t linesDropped() const { return lines_; }

private:
    std::uint64_t begun_ = 0;
    std::uint64_t ended_ = 0;
    std::uint64_t lines_ = 0;
};

}
