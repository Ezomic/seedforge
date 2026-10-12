#pragma once

#include "ichor/core/fixed_step.h"
#include "ichor/sim/scene.h"

#include <cstdint>

namespace ichor {

class Platform;
class Renderer;

struct EngineConfig {
    std::uint64_t seed = 1;
    double fixedDt = 1.0 / 60.0;
    int maxStepsPerFrame = 8;
};

// The loop. Platform and renderer are hosts injected by whoever owns the
// process: the app passes SDL and Vulkan, the tests pass the null backends.
class Engine {
public:
    Engine(Platform& platform, Renderer& renderer, const EngineConfig& config);

    // One real-time frame. Returns false once the platform asks to quit.
    bool frame(double frameSeconds);

    std::uint64_t frameCount() const { return frameCount_; }
    const Scene& scene() const { return scene_; }
    Scene& scene() { return scene_; }

private:
    Platform& platform_;
    Renderer& renderer_;
    FixedStep step_;
    Scene scene_;
    std::uint64_t frameCount_ = 0;
};

}
