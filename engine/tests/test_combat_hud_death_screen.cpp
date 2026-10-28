#include "ichor/ui/death_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace ichor;

namespace {

struct Fixture {
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 1280, 720};
    DeathScreen screen{ui};

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

TEST_CASE("the death screen is hidden and draws nothing until a death is bound", "[death_screen]")
{
    Fixture fixture;
    REQUIRE(fixture.screen.ok());
    CHECK_FALSE(fixture.screen.visible());
    CHECK(fixture.frame(0.0) == 0);
}

TEST_CASE("a bound death shows the title, the cause line and the retry key", "[death_screen]")
{
    Fixture fixture;
    fixture.screen.bind(DeathScreenView{"You ran out of stamina and could not guard or dodge.", "Press Enter to try again."});
    CHECK(fixture.screen.visible());
    CHECK(fixture.frame(0.0) > 0);
    const std::string markup = fixture.screen.markup();
    CHECK(markup.find("YOU DIED") != std::string::npos);
    CHECK(markup.find("You ran out of stamina and could not guard or dodge.") != std::string::npos);
    CHECK(markup.find("Press Enter to try again.") != std::string::npos);
}

TEST_CASE("binding no death hides the screen again", "[death_screen]")
{
    Fixture fixture;
    fixture.screen.bind(DeathScreenView{"You blocked too early or too late.", "Press Enter to try again."});
    fixture.frame(0.0);
    fixture.screen.bind(std::nullopt);
    CHECK_FALSE(fixture.screen.visible());
    CHECK(fixture.frame(0.1) == 0);
}

TEST_CASE("the cause text is escaped, and an empty one still shows the screen", "[death_screen]")
{
    Fixture fixture;
    fixture.screen.bind(DeathScreenView{"<b>&", ""});
    fixture.frame(0.0);
    CHECK(fixture.screen.markup().find("<b>") == std::string::npos);
    fixture.screen.bind(DeathScreenView{"", ""});
    CHECK(fixture.screen.visible());
    CHECK(fixture.frame(0.1) > 0);
}
