#include "ichor/ui/loading_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace ichor;

namespace {

struct Fixture {
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 1280, 720};
    Fixture()
    {
        const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
        REQUIRE(ui.ok());
        REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
        REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    }
    void frame(double time)
    {
        renderer.clearDraws();
        ui.update(time);
        ui.render();
    }
};

ForgeProgressSnapshot snapshot(const char* phase, std::size_t completed, std::size_t total)
{
    ForgeProgressSnapshot s;
    s.started = true;
    s.phase = phase;
    s.completed = completed;
    s.total = total;
    s.done = completed == total;
    return s;
}

}

TEST_CASE("the loading view names the phase and rounds the fraction to a percent", "[loading_screen]")
{
    CHECK(buildLoadingView(snapshot("rivers", 1, 3)) == LoadingView{"rivers", 33});
    CHECK(buildLoadingView(snapshot("roads", 3, 3)).percent == 100);
    const LoadingView idle = buildLoadingView(ForgeProgressSnapshot{});
    CHECK(idle.percent == 0);
    CHECK_FALSE(idle.label.empty());
}

TEST_CASE("the loading view clamps a count that runs past the total", "[loading_screen]")
{
    CHECK(buildLoadingView(snapshot("x", 9, 3)).percent == 100);
    CHECK(buildLoadingView(snapshot("x", 1, 0)).percent == 0);
}

TEST_CASE("the loading screen draws nothing until shown and the bar follows the view", "[loading_screen]")
{
    Fixture f;
    LoadingScreen screen(f.ui);
    REQUIRE(screen.ok());
    screen.setView(LoadingView{"terrain", 40});
    f.frame(0.0);
    REQUIRE(f.renderer.draws().empty());

    screen.setVisible(true);
    f.frame(0.1);
    CHECK_FALSE(f.renderer.draws().empty());
    CHECK(screen.markup().find("terrain") != std::string::npos);
    CHECK(screen.markup().find("40%") != std::string::npos);

    screen.setView(LoadingView{"<b>rivers</b>", 250});
    CHECK(screen.markup().find("<b>") == std::string::npos);
    CHECK(screen.markup().find("100%") != std::string::npos);

    screen.setVisible(false);
    f.frame(0.2);
    CHECK(f.renderer.draws().empty());
}
