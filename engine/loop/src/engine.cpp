#include "ichor/loop/engine.h"

#include "ichor/gfx/renderer.h"
#include "ichor/platform/platform.h"

#include <format>

namespace ichor {

Engine::Engine(Platform& platform, Renderer& renderer, const EngineConfig& config)
    : platform_(platform)
    , renderer_(renderer)
    , step_(config.fixedDt, config.maxStepsPerFrame)
    , scene_(config.seed)
{
}

bool Engine::frame(double frameSeconds)
{
    platform_.pumpEvents();
    if (platform_.quitRequested()) {
        return false;
    }

    const int steps = step_.advance(frameSeconds);
    for (int i = 0; i < steps; ++i) {
        scene_.tick(step_.dt());
    }

    renderer_.beginFrame();
    renderer_.debugText(std::format("frame {}  tick {}  sim {:.2f} s", frameCount_, scene_.tickCount(), scene_.time()));
    renderer_.debugText(std::format("seed {}  state {:016x}", scene_.seed(), scene_.stateHash()));
    renderer_.endFrame();

    ++frameCount_;
    return true;
}

}
