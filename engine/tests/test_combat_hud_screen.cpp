#include "ichor/ui/combat_hud.h"
#include "ichor/ui/combat_hud_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>

using namespace ichor;

namespace {

struct Fixture {
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 1280, 720};
    CombatHudScreen screen{ui, std::filesystem::path(ICHOR_REPO_DATA) / "ui" / "combat_hud.rml"};

    Fixture()
    {
        REQUIRE(ui.ok());
        REQUIRE(ui.loadFontFace(std::filesystem::path(ICHOR_ART_DIR) / "fonts" / "AlegreyaSans-Regular.ttf"));
    }

    void frame(double time)
    {
        renderer.clearDraws();
        ui.update(time);
        ui.render();
    }

    int drawsOf(int r, int g, int b) const
    {
        const auto& draws = renderer.draws();
        return static_cast<int>(std::count_if(draws.begin(), draws.end(), [&](const RecordingUiRenderer::Draw& draw) {
            return draw.firstColour.red == r && draw.firstColour.green == g && draw.firstColour.blue == b;
        }));
    }
};

constexpr int kHealth[] = {168, 50, 50};
constexpr int kLowHealth[] = {224, 64, 64};
constexpr int kLaw[] = {216, 176, 72};
constexpr int kHit[] = {224, 48, 48};
constexpr int kStagger[] = {232, 216, 168};

CombatHudView fullView()
{
    CombatHudInput input;
    input.health = 80.0f;
    input.maxHealth = 100.0f;
    input.stamina = 50.0f;
    input.maxStamina = 100.0f;
    input.lawResource = CombatHudLawResource{"favour", 3.0f, 6.0f};
    input.recentHits = {{0.0f, 0.0f}, {1.5f, 0.0f}};
    input.target = CombatHudTarget{4.0f, 8.0f};
    return buildCombatHudView(input);
}

}

TEST_CASE("the combat HUD document loads and draws nothing until a view is bound", "[combat_hud_screen]")
{
    Fixture fixture;
    REQUIRE(fixture.screen.ok());
    fixture.frame(0.0);
    REQUIRE(fixture.drawsOf(kHealth[0], kHealth[1], kHealth[2]) == 0);
    REQUIRE(fixture.drawsOf(kHit[0], kHit[1], kHit[2]) == 0);
    REQUIRE(fixture.drawsOf(kLaw[0], kLaw[1], kLaw[2]) == 0);
    REQUIRE(fixture.drawsOf(kStagger[0], kStagger[1], kStagger[2]) == 0);
}

TEST_CASE("a bound view draws the health bar, law resource, hit markers and stagger tick", "[combat_hud_screen]")
{
    Fixture fixture;
    REQUIRE(fixture.screen.ok());
    fixture.screen.bind(fullView());
    fixture.frame(0.0);
    REQUIRE(fixture.drawsOf(kHealth[0], kHealth[1], kHealth[2]) == 1);
    REQUIRE(fixture.drawsOf(kLaw[0], kLaw[1], kLaw[2]) == 1);
    REQUIRE(fixture.drawsOf(kHit[0], kHit[1], kHit[2]) == 2);
    REQUIRE(fixture.drawsOf(kStagger[0], kStagger[1], kStagger[2]) == 1);
    REQUIRE(fixture.screen.markup().find("favour") != std::string::npos);
}

TEST_CASE("low health turns the health bar its warning colour", "[combat_hud_screen]")
{
    Fixture fixture;
    CombatHudInput input;
    input.health = 10.0f;
    input.maxHealth = 100.0f;
    input.stamina = 100.0f;
    input.maxStamina = 100.0f;
    fixture.screen.bind(buildCombatHudView(input));
    fixture.frame(0.0);
    REQUIRE(fixture.drawsOf(kLowHealth[0], kLowHealth[1], kLowHealth[2]) == 1);
    REQUIRE(fixture.drawsOf(kHealth[0], kHealth[1], kHealth[2]) == 0);
}

TEST_CASE("rebinding with nothing to show hides the law resource, hits and stagger tick", "[combat_hud_screen]")
{
    Fixture fixture;
    fixture.screen.bind(fullView());
    fixture.frame(0.0);
    CombatHudInput input;
    input.health = 100.0f;
    input.maxHealth = 100.0f;
    fixture.screen.bind(buildCombatHudView(input));
    fixture.frame(0.1);
    REQUIRE(fixture.drawsOf(kLaw[0], kLaw[1], kLaw[2]) == 0);
    REQUIRE(fixture.drawsOf(kHit[0], kHit[1], kHit[2]) == 0);
    REQUIRE(fixture.drawsOf(kStagger[0], kStagger[1], kStagger[2]) == 0);
    REQUIRE(fixture.drawsOf(kHealth[0], kHealth[1], kHealth[2]) == 1);
}

TEST_CASE("hostile view values draw without crashing", "[combat_hud_screen]")
{
    Fixture fixture;
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();
    CombatHudView view;
    view.health.fraction = nan;
    view.stamina.fraction = inf;
    view.lawResource = CombatHudLawView{"<b>&", inf, nan};
    view.indicators = {{nan, nan}, {inf, 5.0f}, {-1e30f, -3.0f}};
    view.targetStagger = nan;
    fixture.screen.bind(view);
    fixture.frame(0.0);
    REQUIRE(fixture.screen.markup().find("<b>") == std::string::npos);
}

TEST_CASE("a missing document leaves the screen not ok and bind harmless", "[combat_hud_screen]")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 640, 360);
    CombatHudScreen screen(ui, std::filesystem::path(ICHOR_REPO_DATA) / "ui" / "missing.rml");
    REQUIRE_FALSE(screen.ok());
    screen.bind(fullView());
}
