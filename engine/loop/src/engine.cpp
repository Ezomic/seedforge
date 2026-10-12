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

void Engine::replayInputs(InputStream stream)
{
    replay_ = std::move(stream);
    replayIndex_ = 0;
    replaying_ = true;
}

InputFrame Engine::nextInput()
{
    InputFrame input;
    if (replaying_) {
        if (replayIndex_ < replay_.frames.size()) {
            input = replay_.frames[replayIndex_++];
        }
    } else {
        input = platform_.input();
    }
    if (recording_) {
        recorded_.frames.push_back(input);
    }
    return input;
}

bool Engine::frame(double frameSeconds)
{
    platform_.pumpEvents();
    if (platform_.quitRequested()) {
        return false;
    }

    const int steps = step_.advance(frameSeconds);
    for (int i = 0; i < steps && !replayFinished(); ++i) {
        scene_.tick(step_.dt(), nextInput());
    }

    renderer_.beginFrame();
    if (viewBuilder_) {
        view_.instances.clear();
        view_.lines.clear();
        viewBuilder_(scene_, step_.alpha(), view_);
        renderer_.submit(view_);
    }
    renderer_.debugText(std::format("frame {}  tick {}  sim {:.2f} s{}", frameCount_, scene_.tickCount(), scene_.time(),
        replaying_ ? "  [replay]" : recording_ ? "  [rec]" : ""));
    renderer_.debugText(std::format("seed {}  state {:016x}  entities {}", scene_.seed(), scene_.stateHash(), scene_.registry().view<const StableId>().size()));
    renderer_.endFrame();

    ++frameCount_;
    return true;
}

}
