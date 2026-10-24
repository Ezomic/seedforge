#pragma once

#include "ichor/core/fixed_step.h"
#include "ichor/core/frame_stages.h"
#include "ichor/core/input.h"
#include "ichor/core/controls_overlay.h"
#include "ichor/core/overlay_mode.h"
#include "ichor/gfx/render_view.h"
#include "ichor/sim/scene.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ichor {

class AudioHost;
class Platform;
class Renderer;

struct EngineConfig {
    std::uint64_t seed = 1;
    double fixedDt = 1.0 / 60.0;
    int maxStepsPerFrame = 8;
};

// The loop. Platform and renderer are hosts injected by whoever owns the
// process: the app passes SDL and Vulkan, the tests pass the null backends.
// Input reaches the scene as a stream of per-tick frames; the stream is
// recorded when asked and can be replayed in place of the platform.
class Engine {
public:
    using ViewBuilder = std::function<void(const Scene& scene, double alpha, RenderView& view)>;
    using OverlayBuilder = std::function<void(const Scene& scene, std::vector<std::string>& lines)>;
    using TickGate = std::function<bool(const Scene& scene)>;
    using CardLinesBuilder = std::function<void(std::vector<std::string>& lines)>;

    Engine(Platform& platform, Renderer& renderer, const EngineConfig& config);

    // Called once per frame between the simulation steps and the submit, with
    // the interpolation alpha for rendering between two fixed steps.
    void setViewBuilder(ViewBuilder builder) { viewBuilder_ = std::move(builder); }
    void setOverlayBuilder(OverlayBuilder builder) { overlayBuilder_ = std::move(builder); }
    // Lines drawn every frame under the controls card whatever the F3 mode, such as the sound legend card.
    void setCardLinesBuilder(CardLinesBuilder builder) { cardLinesBuilder_ = std::move(builder); }
    // F3 steps it round off, compact and full.
    void setOverlayMode(OverlayMode mode) { overlayToggle_.mode = mode; }
    OverlayMode overlayMode() const { return overlayToggle_.mode; }
    // F1 opens the controls card; the state also carries the first-run hint line.
    void setControlsOverlay(ControlsOverlay state) { controls_ = state; }
    const ControlsOverlay& controlsOverlay() const { return controls_; }

    // While the gate says no, no simulation tick runs and no time is owed
    // afterwards; rendering continues. Streaming holds the world here until
    // the cells the player needs are resident.
    void setTickGate(TickGate gate) { tickGate_ = std::move(gate); }
    bool gated() const { return gated_; }

    // Optional: receives the scene's sound events each tick and the camera as listener.
    void setAudio(AudioHost* audio) { audio_ = audio; }

    void recordInputs(bool enabled) { recording_ = enabled; }
    const InputStream& recordedInputs() const { return recorded_; }

    // Feed these frames instead of the platform's, one per tick. Once the
    // stream runs out the loop reports replayFinished().
    void replayInputs(InputStream stream);
    bool replaying() const { return replaying_; }
    bool replayFinished() const { return replaying_ && replayIndex_ >= replay_.frames.size(); }

    // Feed the frames this function makes from the scene, one per tick, instead of the platform's. A replay still
    // takes precedence; the frames are recorded like any other.
    using InputSource = std::function<InputFrame(Scene& scene)>;
    void setInputSource(InputSource source) { inputSource_ = std::move(source); }

    // One real-time frame. Returns false once the platform asks to quit.
    bool frame(double frameSeconds);

    std::uint64_t frameCount() const { return frameCount_; }
    // The time a frame takes, smoothed over the last few dozen, in milliseconds; zero before the first. A paused
    // frame, which has no time of its own, leaves it as it was.
    double frameMilliseconds() const { return frameMillis_; }
    // Where each frame's time goes: sim, view, begin (waits for the GPU's last use of the slot), submit, text,
    // present (the swapchain wait). Callers add their own stages, such as the streamer's cell uploads.
    FrameStages& stages() { return stages_; }
    const FrameStages& stages() const { return stages_; }
    const Scene& scene() const { return scene_; }
    Scene& scene() { return scene_; }

private:
    InputFrame nextInput();

    Platform& platform_;
    Renderer& renderer_;
    AudioHost* audio_ = nullptr;
    FixedStep step_;
    Scene scene_;
    RenderView view_;
    ViewBuilder viewBuilder_;
    OverlayBuilder overlayBuilder_;
    CardLinesBuilder cardLinesBuilder_;
    TickGate tickGate_;
    InputSource inputSource_;
    bool gated_ = false;
    std::vector<std::string> overlay_;
    OverlayToggle overlayToggle_;
    ControlsOverlay controls_;
    InputStream recorded_;
    InputStream replay_;
    std::size_t replayIndex_ = 0;
    bool recording_ = false;
    bool replaying_ = false;
    std::uint64_t frameCount_ = 0;
    double frameMillis_ = 0.0;
    FrameStages stages_;
};

}
