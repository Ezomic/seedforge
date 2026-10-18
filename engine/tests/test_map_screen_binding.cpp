#include "ichor/ui/map_screen_binding.h"
#include "ichor/ui/recording_renderer.h"

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
    std::size_t drawCount()
    {
        renderer.clearDraws();
        ui.update(0.0);
        ui.render();
        return renderer.draws().size();
    }
};

MapScreenView sampleView()
{
    MapScreenView view;
    view.valid = true;
    MapScreenAnnotation mark;
    mark.id = "a";
    mark.kind = AnnotationKind::Mark;
    mark.symbol = AnnotationSymbol::Camp;
    mark.text = "Base <camp>";
    mark.points = {{100.0f, 80.0f}};
    view.annotations.push_back(mark);
    view.hiddenAnnotations = 3;
    MapScreenPlayer player;
    player.position = {200.0f, 200.0f};
    player.onScreen = true;
    player.heading = 1.5707964f;
    view.player = player;
    MapScreenWaypoint waypoint;
    waypoint.id = "w";
    waypoint.position = {300.0f, 150.0f};
    waypoint.onScreen = true;
    waypoint.distance = 420.0f;
    view.waypoint = waypoint;
    return view;
}

}

TEST_CASE("the map binding shows the model's marks, player and waypoint")
{
    Fixture f;
    MapScreenBinding binding(f.ui);
    REQUIRE(binding.ok());
    binding.setVisible(true);
    const std::size_t empty = f.drawCount();

    binding.bind(sampleView());
    CHECK(binding.textOf("list").find("Base &lt;camp&gt;") != std::string::npos);
    CHECK(binding.textOf("list").find("camp") != std::string::npos);
    CHECK(binding.textOf("player").find("Facing 90 degrees") != std::string::npos);
    CHECK(binding.textOf("waypoint").find("420 away") != std::string::npos);
    CHECK(binding.textOf("hidden").find("3 more") != std::string::npos);
    CHECK(f.drawCount() > empty); // the list text and three dots are extra geometry
}

TEST_CASE("an invalid or empty map view clears the binding")
{
    Fixture f;
    MapScreenBinding binding(f.ui);
    REQUIRE(binding.ok());
    binding.bind(sampleView());
    binding.bind(MapScreenView{});
    CHECK(binding.textOf("list").empty());
    CHECK(binding.textOf("player") == "Position unknown.");
    CHECK(binding.textOf("waypoint") == "None.");
}

TEST_CASE("the map binding draws nothing while hidden")
{
    Fixture f;
    MapScreenBinding binding(f.ui);
    binding.bind(sampleView());
    CHECK(f.drawCount() == 0);
}
