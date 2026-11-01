#include "ichor/core/controls_overlay.h"
#include "ichor/core/first_prompts.h"
#include "ichor/core/input.h"
#include "ichor/ui/notice_screen.h"
#include "ichor/ui/prompt_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;
const FirstPromptsFrame kWalking{.canMove = true};
const FirstPromptsFrame kNearFoe{.canMove = true, .foeInRange = true};

// The line shown on each frame, for `frames` frames of the same input.
std::vector<std::optional<std::string>> run(FirstPrompts& state, const FirstPromptsFrame& frame, int frames)
{
    std::vector<std::optional<std::string>> lines;
    for (int i = 0; i < frames; ++i) {
        state = stepFirstPrompts(state, frame, kDt);
        lines.push_back(firstPromptLine(state, frame.held));
    }
    return lines;
}

int framesShowing(const std::vector<std::optional<std::string>>& lines, const std::string& text)
{
    return static_cast<int>(std::ranges::count(lines, std::optional<std::string>(text)));
}

const int kPromptFrames = static_cast<int>(kFirstPromptSeconds / kDt);

}

TEST_CASE("each prompt names the same keys as the F1 card", "[first_prompts]")
{
    CHECK(firstPromptText(FirstPrompt::Move) == "W A S D to move");
    CHECK(firstPromptText(FirstPrompt::Attack) == "J or Left click to attack");
    CHECK(firstPromptText(FirstPrompt::Block) == "Hold L or Middle btn to block");
    CHECK(firstPromptText(FirstPrompt::Dodge) == "Space to dodge");
    ControlsOverlay card;
    card.open = true;
    const std::vector<std::string> lines = controlsOverlayLines(card);
    for (const std::string_view keys : {kMoveKeys, kAttackKeys, kBlockKeys, kDodgeKeys}) {
        INFO(keys);
        CHECK(std::ranges::any_of(lines, [&](const std::string& line) { return line.starts_with(keys); }));
    }
}

TEST_CASE("nothing shows before an action is possible", "[first_prompts]")
{
    FirstPrompts state;
    for (const auto& line : run(state, {}, 600)) {
        CHECK_FALSE(line.has_value());
    }
    CHECK(state.seen == 0);
}

TEST_CASE("the move prompt shows once the player can move, for its time, and sets its flag as it appears", "[first_prompts]")
{
    FirstPrompts state;
    state = stepFirstPrompts(state, kWalking, kDt);
    CHECK(firstPromptLine(state, false) == "W A S D to move");
    CHECK(state.seen == firstPromptBit(FirstPrompt::Move));
    const auto lines = run(state, kWalking, kPromptFrames + 10);
    const int shown = framesShowing(lines, "W A S D to move");
    CHECK(shown >= kPromptFrames - 2);
    CHECK(shown <= kPromptFrames + 1);
    CHECK_FALSE(lines.back().has_value());
}

TEST_CASE("a prompt whose flag is set never shows again", "[first_prompts]")
{
    FirstPrompts state;
    run(state, kWalking, kPromptFrames + 10);
    for (const auto& line : run(state, kWalking, 3000)) {
        CHECK_FALSE(line.has_value());
    }

    FirstPrompts stored;
    stored.seen = kAllFirstPrompts;
    for (const auto& line : run(stored, kNearFoe, 3000)) {
        CHECK_FALSE(line.has_value());
    }
    CHECK(stored.seen == kAllFirstPrompts);
}

TEST_CASE("doing the action ends its prompt at once, and an action done before its prompt skips it", "[first_prompts]")
{
    FirstPrompts state;
    state = stepFirstPrompts(state, kWalking, kDt);
    REQUIRE(state.showing == FirstPrompt::Move);
    FirstPromptsFrame moving = kWalking;
    moving.moveY = 1.0f;
    state = stepFirstPrompts(state, moving, kDt);
    CHECK_FALSE(firstPromptLine(state, false).has_value());

    FirstPromptsFrame swung = kWalking;
    swung.buttons = InputFrame::Attack;
    state = stepFirstPrompts(state, swung, kDt);
    CHECK((state.seen & firstPromptBit(FirstPrompt::Attack)) != 0);
    const auto lines = run(state, kNearFoe, 3 * kPromptFrames);
    CHECK(framesShowing(lines, "J or Left click to attack") == 0);
    CHECK(framesShowing(lines, "Hold L or Middle btn to block") > 0);
}

TEST_CASE("prompts that become possible on the same frame queue one after another and never overlap", "[first_prompts]")
{
    FirstPrompts state;
    state.seen = firstPromptBit(FirstPrompt::Move);
    const auto lines = run(state, kNearFoe, 3 * kPromptFrames + 30);
    std::vector<std::string> order;
    for (const auto& line : lines) {
        if (line && (order.empty() || order.back() != *line)) {
            order.push_back(*line);
        }
    }
    CHECK(order == std::vector<std::string>{"J or Left click to attack", "Hold L or Middle btn to block", "Space to dodge"});
    CHECK(state.seen == kAllFirstPrompts);
    CHECK_FALSE(lines.back().has_value());
}

TEST_CASE("a queued prompt still shows after the foe walks away", "[first_prompts]")
{
    FirstPrompts state;
    state.seen = firstPromptBit(FirstPrompt::Move);
    run(state, kNearFoe, 1);
    REQUIRE(state.showing == FirstPrompt::Attack);
    const auto lines = run(state, kWalking, 3 * kPromptFrames + 30);
    CHECK(framesShowing(lines, "Space to dodge") > 0);
}

TEST_CASE("a held frame hides the prompt, stops its time, starts none and ignores the keys", "[first_prompts]")
{
    FirstPrompts state;
    FirstPromptsFrame held = kNearFoe;
    held.held = true;
    held.buttons = InputFrame::Attack | InputFrame::Block | InputFrame::Dodge;
    held.moveX = 1.0f;
    for (const auto& line : run(state, held, 600)) {
        CHECK_FALSE(line.has_value());
    }
    CHECK(state.seen == 0);
    CHECK_FALSE(state.showing.has_value());

    state = stepFirstPrompts(state, kWalking, kDt);
    REQUIRE(state.showing == FirstPrompt::Move);
    const double left = state.secondsLeft;
    held.buttons = 0;
    held.moveX = 0.0f;
    run(state, held, 600);
    CHECK(state.secondsLeft == left);
    CHECK(state.showing == FirstPrompt::Move);
    CHECK_FALSE(firstPromptLine(state, true).has_value());
    CHECK(firstPromptLine(state, false) == "W A S D to move");
}

TEST_CASE("a negative, NaN or infinite frame time does not run a prompt down", "[first_prompts]")
{
    FirstPrompts state = stepFirstPrompts({}, kWalking, kDt);
    const double left = state.secondsLeft;
    for (const double dt : {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        state = stepFirstPrompts(state, kWalking, dt);
        CHECK(state.secondsLeft == left);
        CHECK(state.showing == FirstPrompt::Move);
    }
}

TEST_CASE("only a person at the window gets prompts: never headless, a screenshot, a replay or a bot", "[first_prompts]")
{
    CHECK(firstPromptsShown({}));
    CHECK_FALSE(firstPromptsShown({.headless = true}));
    CHECK_FALSE(firstPromptsShown({.screenshot = true}));
    CHECK_FALSE(firstPromptsShown({.replay = true}));
    CHECK_FALSE(firstPromptsShown({.bot = true}));
}

namespace {

struct Fixture {
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 1280, 720};
    PromptScreen screen{ui};

    Fixture()
    {
        REQUIRE(ui.ok());
        REQUIRE(ui.loadFontFace(std::filesystem::path(ICHOR_ART_DIR) / "fonts" / "AlegreyaSans-Regular.ttf"));
    }

    std::size_t frame(double time)
    {
        renderer.clearDraws();
        ui.update(time);
        ui.render();
        return renderer.draws().size();
    }
};

}

TEST_CASE("the prompt screen draws nothing until a line is bound, shows it, and hides again", "[first_prompts]")
{
    Fixture fixture;
    REQUIRE(fixture.screen.ok());
    CHECK_FALSE(fixture.screen.visible());
    CHECK(fixture.frame(0.0) == 0);
    fixture.screen.bind(std::string("W A S D to move"));
    CHECK(fixture.screen.visible());
    CHECK(fixture.frame(0.1) > 0);
    CHECK(fixture.screen.markup().find("W A S D to move") != std::string::npos);
    fixture.screen.bind(std::nullopt);
    CHECK_FALSE(fixture.screen.visible());
    CHECK(fixture.frame(0.2) == 0);
}

TEST_CASE("the prompt text is escaped", "[first_prompts]")
{
    Fixture fixture;
    fixture.screen.bind(std::string("<b>A & B</b>"));
    CHECK(fixture.screen.markup().find("<b>") == std::string::npos);
    CHECK(fixture.screen.markup().find("&amp;") != std::string::npos);
}

TEST_CASE("one long frame, such as a stall, runs a prompt down by a short step only", "[first_prompts]")
{
    FirstPrompts state = stepFirstPrompts({}, kWalking, kDt);
    state = stepFirstPrompts(state, kWalking, 9.0);
    CHECK(state.showing == FirstPrompt::Move);
    CHECK(state.secondsLeft >= kFirstPromptSeconds - 2 * kFirstPromptMaxStep);
}

TEST_CASE("the notice screen draws nothing until a line is bound, shows it escaped, and hides again", "[first_prompts]")
{
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 1280, 720};
    NoticeScreen screen{ui};
    REQUIRE(ui.ok());
    REQUIRE(ui.loadFontFace(std::filesystem::path(ICHOR_ART_DIR) / "fonts" / "AlegreyaSans-Regular.ttf"));
    REQUIRE(screen.ok());
    const auto draws = [&](double time) {
        renderer.clearDraws();
        ui.update(time);
        ui.render();
        return renderer.draws().size();
    };
    CHECK(draws(0.0) == 0);
    screen.bind(std::string("Slot 'a<b>' restored"));
    CHECK(screen.visible());
    CHECK(draws(0.1) > 0);
    CHECK(screen.markup().find("<b>") == std::string::npos);
    screen.bind(std::nullopt);
    CHECK(draws(0.2) == 0);
}
