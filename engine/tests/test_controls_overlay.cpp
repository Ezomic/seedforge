#include "ichor/core/controls_overlay.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

using namespace ichor;

namespace {

bool anyLineContains(const std::vector<std::string>& lines, const std::string& text)
{
    return std::ranges::any_of(lines, [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

}

TEST_CASE("the controls card starts closed and a press of F1 opens it, the next closes it")
{
    ControlsOverlay state = startControlsOverlay(false, false);
    REQUIRE_FALSE(state.open);
    state = stepControlsOverlay(state, true, 0.016);
    REQUIRE(state.open);
    state = stepControlsOverlay(state, false, 0.016);
    REQUIRE(state.open);
    state = stepControlsOverlay(state, true, 0.016);
    REQUIRE_FALSE(state.open);
}

TEST_CASE("a held F1 toggles the card once, not every frame")
{
    ControlsOverlay state = startControlsOverlay(false, false);
    for (int i = 0; i < 100; ++i) {
        state = stepControlsOverlay(state, true, 0.016);
    }
    REQUIRE(state.open);
}

TEST_CASE("the card can start open for a screenshot and a first run does not also show the hint")
{
    const ControlsOverlay state = startControlsOverlay(true, true);
    REQUIRE(state.open);
    REQUIRE_FALSE(controlsHintVisible(state));
}

TEST_CASE("the first-run hint shows, runs out, and a later run never shows it")
{
    ControlsOverlay first = startControlsOverlay(true, false);
    REQUIRE(controlsHintVisible(first));
    REQUIRE(controlsOverlayLines(first).size() == 1);
    REQUIRE(anyLineContains(controlsOverlayLines(first), "F1"));
    for (double t = 0.0; t < kControlsHintSeconds - 1.0; t += 1.0) {
        first = stepControlsOverlay(first, false, 1.0);
    }
    REQUIRE(controlsHintVisible(first));
    first = stepControlsOverlay(first, false, 2.0);
    REQUIRE_FALSE(controlsHintVisible(first));
    REQUIRE(controlsOverlayLines(first).empty());

    REQUIRE_FALSE(controlsHintVisible(startControlsOverlay(false, false)));
}

TEST_CASE("opening the card ends the hint for good")
{
    ControlsOverlay state = startControlsOverlay(true, false);
    state = stepControlsOverlay(state, true, 0.016);
    state = stepControlsOverlay(state, false, 0.016);
    state = stepControlsOverlay(state, true, 0.016);
    REQUIRE_FALSE(state.open);
    REQUIRE_FALSE(controlsHintVisible(state));
}

TEST_CASE("a hostile frame time never moves the hint timer the wrong way")
{
    const double bad[] = {-5.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), 0.0};
    for (const double dt : bad) {
        const ControlsOverlay state = stepControlsOverlay(startControlsOverlay(true, false), false, dt);
        REQUIRE(state.hintSecondsLeft == kControlsHintSeconds);
    }
}

TEST_CASE("the card lists every control the ticket names and the lines are stable")
{
    ControlsOverlay open;
    open.open = true;
    const std::vector<std::string> lines = controlsOverlayLines(open);
    for (const char* needed : {"W A S D", "Right mouse", "attack", "block", "dodge", "interact", "map", "journal", "F3", "F1"}) {
        INFO(needed);
        REQUIRE(anyLineContains(lines, needed));
    }
    REQUIRE(lines == controlsOverlayLines(open));
    REQUIRE(std::ranges::none_of(lines, [](const std::string& line) { return line.empty(); }));
}

TEST_CASE("pressing F1 in the engine opens the card, which draws its lines, and a second press closes it")
{
    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(platform.init({}));
    REQUIRE(renderer.init());
    Engine engine(platform, renderer, {});
    engine.setOverlayMode(OverlayMode::Off);

    const auto linesInOneFrame = [&] {
        const std::uint64_t before = renderer.linesDropped();
        REQUIRE(engine.frame(1.0 / 60.0));
        return renderer.linesDropped() - before;
    };
    REQUIRE(linesInOneFrame() == 0);
    platform.setHelpKey(true);
    const std::uint64_t cardLines = linesInOneFrame();
    REQUIRE(engine.controlsOverlay().open);
    REQUIRE(cardLines == controlsOverlayLines(engine.controlsOverlay()).size());
    REQUIRE(linesInOneFrame() > 10);
    platform.setHelpKey(false);
    REQUIRE(linesInOneFrame() > 10);
    platform.setHelpKey(true);
    REQUIRE(linesInOneFrame() == 0);
    REQUIRE_FALSE(engine.controlsOverlay().open);
}

TEST_CASE("the engine shows the first-run hint as one line until it runs out")
{
    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(platform.init({}));
    REQUIRE(renderer.init());
    Engine engine(platform, renderer, {});
    engine.setOverlayMode(OverlayMode::Off);
    engine.setControlsOverlay(startControlsOverlay(true, false));

    std::uint64_t before = renderer.linesDropped();
    REQUIRE(engine.frame(1.0 / 60.0));
    REQUIRE(renderer.linesDropped() - before == 1);
    REQUIRE(engine.frame(kControlsHintSeconds));
    before = renderer.linesDropped();
    REQUIRE(engine.frame(1.0 / 60.0));
    REQUIRE(renderer.linesDropped() - before == 0);
}

TEST_CASE("the controls card does not change the simulation")
{
    const auto hashWith = [](bool open) {
        NullPlatform platform;
        NullRenderer renderer;
        REQUIRE(platform.init({}));
        REQUIRE(renderer.init());
        EngineConfig config;
        config.seed = 5;
        Engine engine(platform, renderer, config);
        engine.setControlsOverlay(startControlsOverlay(false, open));
        for (int i = 0; i < 30; ++i) {
            REQUIRE(engine.frame(1.0 / 60.0));
        }
        return engine.scene().stateHash();
    };
    REQUIRE(hashWith(false) == hashWith(true));
}
