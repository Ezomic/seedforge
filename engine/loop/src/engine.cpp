#include "ichor/loop/engine.h"

#include "ichor/audio/audio_host.h"
#include "ichor/gfx/renderer.h"
#include "ichor/platform/platform.h"

#include <cmath>
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
    } else if (inputSource_) {
        input = inputSource_(scene_);
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
    if (std::isfinite(frameSeconds) && frameSeconds > 0.0) {
        const double millis = frameSeconds * 1000.0;
        frameMillis_ = frameMillis_ == 0.0 ? millis : frameMillis_ * 0.95 + millis * 0.05;
    }

    if (audio_ != nullptr) {
        // Cues raised this frame are heard from where the camera was last drawn.
        audio_->setListener(view_.camera.position, view_.camera.rotation);
    }
    gated_ = tickGate_ && !tickGate_(scene_);
    if (gated_) {
        step_.reset();
    }
    const int steps = gated_ ? 0 : step_.advance(frameSeconds);
    {
        FrameStages::Scope timer(stages_, "sim");
        for (int i = 0; i < steps && !replayFinished(); ++i) {
            scene_.tick(step_.dt(), nextInput());
            if (audio_ != nullptr) {
                for (const SoundEvent& event : scene_.soundEvents()) {
                    audio_->play(event.sound, event.position, true);
                }
            }
        }
    }

    {
        FrameStages::Scope timer(stages_, "begin");
        renderer_.beginFrame();
    }
    if (viewBuilder_) {
        view_.instances.clear();
        view_.lines.clear();
        view_.jointPalette.clear();
        view_.terrain.clear();
        view_.vegetation.clear();
        view_.waterCells.clear();
        {
            FrameStages::Scope timer(stages_, "view");
            viewBuilder_(scene_, step_.alpha(), view_);
        }
        FrameStages::Scope timer(stages_, "submit");
        renderer_.submit(view_);
    }
    overlayToggle_.update(platform_.overlayKey());
    const double fps = frameMillis_ > 0.0 ? 1000.0 / frameMillis_ : 0.0;
    if (overlayToggle_.mode == OverlayMode::Compact) {
        renderer_.debugText(std::format("{:.0f} fps  {:.1f} ms", fps, frameMillis_));
    } else if (overlayToggle_.mode == OverlayMode::Full) {
        renderer_.debugText(std::format("frame {}  {:.0f} fps  {:.1f} ms  tick {}  sim {:.2f} s{}{}", frameCount_, fps, frameMillis_, scene_.tickCount(), scene_.time(),
            replaying_ ? "  [replay]" : recording_ ? "  [rec]" : "", gated_ ? "  [loading]" : ""));
        renderer_.debugText(std::format("seed {}  state {:016x}  entities {}", scene_.seed(), scene_.stateHash(), scene_.registry().view<const StableId>().size()));
        renderer_.debugText("ms (mean/peak) " + stages_.summary());
    }
    if (overlayBuilder_ && overlayToggle_.mode == OverlayMode::Full) {
        overlay_.clear();
        overlayBuilder_(scene_, overlay_);
        for (std::string& line : overlay_) {
            renderer_.debugText(std::move(line));
        }
    }
    {
        FrameStages::Scope timer(stages_, "present");
        renderer_.endFrame();
    }
    for (const Renderer::GpuPassTime& pass : renderer_.gpuPasses()) {
        stages_.record(std::string("gpu.") + pass.name, pass.ms);
    }
    stages_.endFrame();

    ++frameCount_;
    return true;
}

}
