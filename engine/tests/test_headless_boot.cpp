#include "ichor/loop/engine.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/platform/null_platform.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ichor;

namespace {

std::uint64_t runFrames(std::uint64_t seed, int frames, double dt)
{
    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(platform.init({}));
    REQUIRE(renderer.init());

    EngineConfig config;
    config.seed = seed;
    Engine engine(platform, renderer, config);
    for (int i = 0; i < frames; ++i) {
        REQUIRE(engine.frame(dt));
    }
    REQUIRE(renderer.framesBegun() == static_cast<std::uint64_t>(frames));
    REQUIRE(renderer.framesEnded() == static_cast<std::uint64_t>(frames));
    return engine.scene().stateHash();
}

}

TEST_CASE("the engine boots headless and ticks an empty scene")
{
    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(platform.init({}));
    REQUIRE(renderer.init());

    EngineConfig config;
    config.fixedDt = 0.25;
    Engine engine(platform, renderer, config);

    REQUIRE(engine.frame(0.625));
    REQUIRE(engine.scene().tickCount() == 2);
    REQUIRE(engine.frame(0.125));
    REQUIRE(engine.scene().tickCount() == 3);
    REQUIRE(engine.frameCount() == 2);
    REQUIRE(platform.pumpCount() == 2);
    REQUIRE(renderer.linesDropped() > 0);
}

TEST_CASE("quit stops the loop")
{
    NullPlatform platform;
    NullRenderer renderer;
    Engine engine(platform, renderer, {});
    platform.requestQuit();
    REQUIRE_FALSE(engine.frame(0.016));
    REQUIRE(engine.frameCount() == 0);
}

TEST_CASE("two runs with the same seed end in the same state")
{
    const auto a = runFrames(99, 120, 1.0 / 60.0);
    const auto b = runFrames(99, 120, 1.0 / 60.0);
    REQUIRE(a == b);
    REQUIRE(a != runFrames(100, 120, 1.0 / 60.0));
}

TEST_CASE("the engine reports a smoothed frame time, ignoring frames that have none")
{
    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(platform.init({}));
    REQUIRE(renderer.init());
    Engine engine(platform, renderer, EngineConfig{});
    REQUIRE(engine.frameMilliseconds() == 0.0);

    REQUIRE(engine.frame(0.020));
    REQUIRE(engine.frameMilliseconds() == 20.0); // the first frame is the whole average
    for (int i = 0; i < 200; ++i) {
        REQUIRE(engine.frame(0.010));
    }
    REQUIRE(engine.frameMilliseconds() > 9.99);
    REQUIRE(engine.frameMilliseconds() < 10.1); // the 20 ms has all but washed out

    const double before = engine.frameMilliseconds();
    REQUIRE(engine.frame(0.0));                  // a paused frame
    REQUIRE(engine.frame(-1.0));
    REQUIRE(engine.frame(std::nan("")));
    REQUIRE(engine.frameMilliseconds() == before);
}

TEST_CASE("an input source feeds every tick, is recorded, and yields to a replay")
{
    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(platform.init({}));
    REQUIRE(renderer.init());
    EngineConfig config;
    config.fixedDt = 0.25;
    Engine engine(platform, renderer, config);

    int asked = 0;
    engine.setInputSource([&](Scene&) {
        ++asked;
        InputFrame frame;
        frame.moveX = 0.5f;
        return frame;
    });
    std::vector<float> seen;
    engine.scene().addSystem("probe", [&](Scene&, double, const InputFrame& input) { seen.push_back(input.moveX); });
    engine.recordInputs(true);

    REQUIRE(engine.frame(0.75));
    REQUIRE(asked == 3);
    REQUIRE(seen == std::vector<float>{0.5f, 0.5f, 0.5f});
    REQUIRE(engine.recordedInputs().frames.size() == 3);
    REQUIRE(engine.recordedInputs().frames.back().moveX == 0.5f);

    InputStream stream;
    stream.frames.push_back(InputFrame{});
    stream.frames.back().moveX = -1.0f;
    engine.replayInputs(stream);
    REQUIRE(engine.frame(0.25));
    REQUIRE(asked == 3);
    REQUIRE(seen.back() == -1.0f);
}
