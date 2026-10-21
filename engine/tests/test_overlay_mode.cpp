#include "ichor/core/overlay_mode.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"

#include <catch2/catch_test_macros.hpp>

using namespace ichor;

namespace {

std::vector<OverlayMode> pressTimes(OverlayToggle toggle, const std::vector<bool>& keyPerFrame)
{
    std::vector<OverlayMode> seen;
    for (const bool down : keyPerFrame) {
        toggle.update(down);
        seen.push_back(toggle.mode);
    }
    return seen;
}

}

TEST_CASE("the overlay starts compact and each press steps it to full, then off, then compact")
{
    OverlayToggle toggle;
    REQUIRE(toggle.mode == OverlayMode::Compact);
    const std::vector<OverlayMode> seen = pressTimes(toggle, {true, false, true, false, true, false, true, false});
    const std::vector<OverlayMode> expected = {OverlayMode::Full, OverlayMode::Full, OverlayMode::Off, OverlayMode::Off,
        OverlayMode::Compact, OverlayMode::Compact, OverlayMode::Full, OverlayMode::Full};
    REQUIRE(seen == expected);
}

TEST_CASE("holding the overlay key counts as one press")
{
    const std::vector<OverlayMode> seen = pressTimes(OverlayToggle{}, {true, true, true, true, false, false});
    for (const OverlayMode mode : seen) {
        REQUIRE(mode == OverlayMode::Full);
    }
}

TEST_CASE("a key already down at the start does not step the mode again until it is released")
{
    OverlayToggle toggle;
    toggle.held = true;
    toggle.update(true);
    REQUIRE(toggle.mode == OverlayMode::Compact);
    toggle.update(false);
    toggle.update(true);
    REQUIRE(toggle.mode == OverlayMode::Full);
}

TEST_CASE("the cycle returns to where it began after three steps from any mode")
{
    for (const OverlayMode start : {OverlayMode::Off, OverlayMode::Compact, OverlayMode::Full}) {
        REQUIRE(nextOverlayMode(nextOverlayMode(nextOverlayMode(start))) == start);
        REQUIRE(nextOverlayMode(start) != start);
    }
}

TEST_CASE("overlay mode names parse and anything else is refused")
{
    REQUIRE(parseOverlayMode("off") == OverlayMode::Off);
    REQUIRE(parseOverlayMode("compact") == OverlayMode::Compact);
    REQUIRE(parseOverlayMode("full") == OverlayMode::Full);
    for (const char* bad : {"", "Full", "OFF", " full", "full ", "on", "--screenshot", "out.png"}) {
        REQUIRE_FALSE(parseOverlayMode(bad).has_value());
    }
}

TEST_CASE("a screenshot starts with no overlay unless one is asked for, and play starts compact")
{
    REQUIRE(startingOverlayMode(true, std::nullopt) == OverlayMode::Off);
    REQUIRE(startingOverlayMode(false, std::nullopt) == OverlayMode::Compact);
    for (const OverlayMode asked : {OverlayMode::Off, OverlayMode::Compact, OverlayMode::Full}) {
        REQUIRE(startingOverlayMode(true, asked) == asked);
        REQUIRE(startingOverlayMode(false, asked) == asked);
    }
}

TEST_CASE("the engine draws no overlay lines when off, one when compact and the stage timers when full")
{
    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(platform.init({}));
    REQUIRE(renderer.init());
    Engine engine(platform, renderer, {});
    engine.setOverlayBuilder([](const Scene&, std::vector<std::string>& lines) { lines.push_back("extra"); });

    const auto linesInOneFrame = [&] {
        const std::uint64_t before = renderer.linesDropped();
        REQUIRE(engine.frame(1.0 / 60.0));
        return renderer.linesDropped() - before;
    };

    engine.setOverlayMode(OverlayMode::Off);
    REQUIRE(linesInOneFrame() == 0);
    engine.setOverlayMode(OverlayMode::Compact);
    REQUIRE(linesInOneFrame() == 1);
    engine.setOverlayMode(OverlayMode::Full);
    REQUIRE(linesInOneFrame() == 4);
}

TEST_CASE("pressing F3 in the engine steps the overlay and a held key steps it once")
{
    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(platform.init({}));
    REQUIRE(renderer.init());
    Engine engine(platform, renderer, {});
    REQUIRE(engine.overlayMode() == OverlayMode::Compact);

    platform.setOverlayKey(true);
    REQUIRE(engine.frame(1.0 / 60.0));
    REQUIRE(engine.overlayMode() == OverlayMode::Full);
    REQUIRE(engine.frame(1.0 / 60.0));
    REQUIRE(engine.overlayMode() == OverlayMode::Full);
    platform.setOverlayKey(false);
    REQUIRE(engine.frame(1.0 / 60.0));
    platform.setOverlayKey(true);
    REQUIRE(engine.frame(1.0 / 60.0));
    REQUIRE(engine.overlayMode() == OverlayMode::Off);
}

TEST_CASE("the overlay mode does not change the simulation")
{
    const auto hashWith = [](OverlayMode mode) {
        NullPlatform platform;
        NullRenderer renderer;
        REQUIRE(platform.init({}));
        REQUIRE(renderer.init());
        EngineConfig config;
        config.seed = 5;
        Engine engine(platform, renderer, config);
        engine.setOverlayMode(mode);
        for (int i = 0; i < 30; ++i) {
            REQUIRE(engine.frame(1.0 / 60.0));
        }
        return engine.scene().stateHash();
    };
    REQUIRE(hashWith(OverlayMode::Off) == hashWith(OverlayMode::Full));
    REQUIRE(hashWith(OverlayMode::Compact) == hashWith(OverlayMode::Full));
}
