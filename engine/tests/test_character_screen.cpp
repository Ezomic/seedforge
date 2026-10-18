#include "ichor/ui/character_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace ichor;

namespace {

void frame(UiContext& ui, RecordingUiRenderer& renderer, double time)
{
    renderer.clearDraws();
    ui.update(time);
    ui.render();
}

struct Fixture {
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 1280, 720};
    std::filesystem::path document = std::filesystem::path(ICHOR_REPO_DATA) / "ui" / "character_screen.rml";

    Fixture()
    {
        REQUIRE(ui.ok());
        const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
        REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
        REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    }
};

CharacterScreenInput sample()
{
    CharacterScreenInput input;
    input.stats = {{CharacterStat::Health, 120.0, 100.0}, {CharacterStat::Stamina, 80.0, 80.0}};
    input.capabilities = {{"pick_lock", "Pick locks", true}, {"swim", "Swim deep water", false}};
    input.verbs = {{"parry", "Parry", true}};
    input.standing = {{"Weir Guild", 250, "Trusted", {"Trade"}, "Allied", {"Safe passage"}, 150}};
    return input;
}

}

TEST_CASE("the character screen draws nothing until shown, then more as the model fills it, and only relays out on change")
{
    Fixture f;
    CharacterScreen screen(f.ui, f.document);
    REQUIRE(screen.ok());
    REQUIRE_FALSE(screen.visible());

    screen.bind({});
    frame(f.ui, f.renderer, 0.0);
    REQUIRE(f.renderer.draws().empty());

    screen.setVisible(true);
    frame(f.ui, f.renderer, 0.1);
    const std::size_t empty = f.renderer.draws().size();
    REQUIRE(empty >= 2);

    screen.bind(sample());
    frame(f.ui, f.renderer, 0.2);
    const std::size_t full = f.renderer.draws().size();
    REQUIRE(full > empty);

    screen.bind(sample());
    frame(f.ui, f.renderer, 0.3);
    REQUIRE(f.renderer.draws().size() == full);

    screen.setVisible(false);
    frame(f.ui, f.renderer, 0.4);
    REQUIRE(f.renderer.draws().empty());
}

TEST_CASE("the character screen shows the model's sections, lines and values as text")
{
    Fixture f;
    CharacterScreen screen(f.ui, f.document);
    screen.setVisible(true);
    screen.bind(sample());
    frame(f.ui, f.renderer, 0.0);
    const std::string text = screen.contentRml();
    for (const char* expected : {"Pick locks", "Swim deep water", "Parry", "Weir Guild", "120", "+20%", "Safe passage"}) {
        INFO(expected);
        REQUIRE(text.find(expected) != std::string::npos);
    }
}

TEST_CASE("model text is shown as text, never markup")
{
    Fixture f;
    CharacterScreen screen(f.ui, f.document);
    screen.setVisible(true);
    CharacterScreenInput input;
    input.capabilities = {{"x", "<div style=\"background-color: #ff0000; width: 900px; height: 900px\">hi</div>", true}};
    screen.bind(input);
    frame(f.ui, f.renderer, 0.0);
    for (const auto& draw : f.renderer.draws()) {
        REQUIRE_FALSE((draw.firstColour.red == 255 && draw.firstColour.green == 0 && draw.firstColour.blue == 0));
    }
}

TEST_CASE("a missing document leaves the character screen not ok and binding harmless")
{
    Fixture f;
    CharacterScreen screen(f.ui, "no/such/character_screen.rml");
    REQUIRE_FALSE(screen.ok());
    screen.bind(sample());
    screen.setVisible(true);
    REQUIRE_FALSE(screen.visible());
}

TEST_CASE("clicking the character screen's close button asks for it to be closed, and clicking elsewhere does not")
{
    Fixture f;
    CharacterScreen screen(f.ui, f.document);
    REQUIRE_FALSE(screen.closeButtonCentre().has_value());
    screen.setVisible(true);
    frame(f.ui, f.renderer, 0.0);
    const auto centre = screen.closeButtonCentre();
    REQUIRE(centre.has_value());

    f.ui.mouseMove(640, 400);
    frame(f.ui, f.renderer, 0.2);
    f.ui.mouseButton(0, true);
    f.ui.mouseButton(0, false);
    frame(f.ui, f.renderer, 0.3);
    REQUIRE_FALSE(screen.takeCloseRequest());

    f.ui.mouseMove(static_cast<int>(centre->first), static_cast<int>(centre->second));
    frame(f.ui, f.renderer, 0.4);
    f.ui.mouseButton(0, true);
    frame(f.ui, f.renderer, 0.5);
    f.ui.mouseButton(0, false);
    frame(f.ui, f.renderer, 0.6);
    REQUIRE(screen.takeCloseRequest());
    REQUIRE_FALSE(screen.takeCloseRequest());
}
