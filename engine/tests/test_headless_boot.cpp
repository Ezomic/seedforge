#include "ichor/loop/engine.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/platform/null_platform.h"

#include <catch2/catch_test_macros.hpp>

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
