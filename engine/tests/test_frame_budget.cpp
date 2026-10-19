#include "ichor/core/frame_stages.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

using namespace ichor;

namespace {

constexpr int kFrames = 120;

struct Rig {
    NullPlatform platform;
    NullRenderer renderer;
    Engine engine;

    Rig()
        : engine(platform, renderer, EngineConfig{})
    {
        REQUIRE(platform.init({}));
        REQUIRE(renderer.init());
    }

    void run(int frames)
    {
        for (int i = 0; i < frames; ++i) {
            REQUIRE(engine.frame(1.0 / 60.0));
        }
    }
};

}

TEST_CASE("a draw list the view builder fills each frame does not grow from frame to frame")
{
    Rig rig;
    rig.engine.setViewBuilder([](const Scene&, double, RenderView& view) {
        view.instances.push_back({});
        view.terrain.push_back({});
        view.vegetation.push_back({});
        view.waterCells.push_back({});
    });
    rig.run(kFrames);
    // A list nobody clears holds one more draw every frame, and the GPU pays for all of them.
    REQUIRE(rig.renderer.vegetationDrawsSubmitted() == static_cast<std::uint64_t>(kFrames));
    REQUIRE(rig.renderer.waterDrawsSubmitted() == static_cast<std::uint64_t>(kFrames));
    REQUIRE(rig.renderer.terrainDrawsSubmitted() == static_cast<std::uint64_t>(kFrames));
    REQUIRE(rig.renderer.lastDrawCount() == 4);
}

TEST_CASE("every frame stage is timed once per frame and stays inside a generous budget")
{
    Rig rig;
    rig.engine.setViewBuilder([](const Scene&, double, RenderView& view) { view.instances.push_back({}); });
    rig.run(kFrames);
    for (const char* name : {"sim", "begin", "view", "submit", "present"}) {
        const FrameStages::Stage* stage = rig.engine.stages().get(name);
        REQUIRE(stage != nullptr);
        INFO(name);
        REQUIRE(stage->samples == static_cast<std::uint64_t>(kFrames));
        // Microseconds in practice; the headroom is for sanitizer builds on a busy runner.
        REQUIRE(stage->meanMs < 25.0);
    }
}

TEST_CASE("a slow stage shows up in its own timer and in no other")
{
    Rig rig;
    rig.engine.setViewBuilder([](const Scene&, double, RenderView&) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); });
    rig.run(10);
    const FrameStages& stages = rig.engine.stages();
    REQUIRE(stages.get("view")->meanMs >= 15.0);
    // The typical cost, not the peak: the other stages keep their own, far smaller, mean.
    REQUIRE(stages.get("sim")->meanMs < stages.get("view")->meanMs / 2.0);
    REQUIRE(stages.get("submit")->meanMs < stages.get("view")->meanMs / 2.0);
}

TEST_CASE("stages that run more than once a frame add up to one sample per frame")
{
    FrameStages stages;
    for (int frame = 0; frame < 3; ++frame) {
        stages.accumulate("upload", 2.0);
        stages.accumulate("upload", 3.0);
        stages.endFrame();
    }
    stages.endFrame(); // a frame where nothing ran
    const FrameStages::Stage* upload = stages.get("upload");
    REQUIRE(upload != nullptr);
    REQUIRE(upload->samples == 4);
    REQUIRE(upload->peakMs == 5.0);
    REQUIRE(upload->totalMs == 15.0);
    REQUIRE(upload->lastMs == 0.0);
}

TEST_CASE("bad samples count as zero and a reset forgets everything")
{
    FrameStages stages;
    stages.record("x", std::numeric_limits<double>::quiet_NaN());
    stages.record("x", -4.0);
    stages.record("x", std::numeric_limits<double>::infinity());
    const FrameStages::Stage* x = stages.get("x");
    REQUIRE(x != nullptr);
    REQUIRE(x->samples == 3);
    REQUIRE(std::isfinite(x->totalMs));
    REQUIRE(x->totalMs == 0.0);
    stages.reset();
    REQUIRE(stages.get("x") == nullptr);
    REQUIRE(stages.summary().empty());
}
