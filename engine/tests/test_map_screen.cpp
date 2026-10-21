#include "ichor/ui/map_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"
#include "ichor/world/map_layers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

void frame(UiContext& ui, RecordingUiRenderer& renderer, double time)
{
    renderer.clearDraws();
    ui.update(time);
    ui.render();
}

bool drewWhite(const RecordingUiRenderer& renderer)
{
    const auto& draws = renderer.draws();
    return std::any_of(draws.begin(), draws.end(), [](const RecordingUiRenderer::Draw& draw) {
        return draw.firstColour.red == 255 && draw.firstColour.green == 255 && draw.firstColour.blue == 255;
    });
}

// The hash of the picture's texture in the last frame: the one draw bound to a texture.
std::uint64_t pictureHash(const RecordingUiRenderer& renderer)
{
    for (const auto& draw : renderer.draws()) {
        if (draw.textureHash != 0 && draw.firstColour.red == 255 && draw.firstColour.green == 255 && draw.firstColour.blue == 255) {
            return draw.textureHash;
        }
    }
    return 0;
}

MapImage sampleImage(std::uint32_t side)
{
    MapImage image;
    image.width = side;
    image.height = side;
    image.rgba.assign(static_cast<std::size_t>(side) * side * 4, 255);
    return image;
}

const worldforge::ForgedWorld& layerWorld()
{
    worldforge::FactionSpec a;
    worldforge::FactionSpec b;
    b.strategy = worldforge::ClaimStrategy::ResourceFollowing;
    b.oreWeight = 200;
    worldforge::ForgeConfig config;
    config.factions = {a, b};
    static const worldforge::ForgedWorld world = [&] {
        for (std::uint64_t seed = 1; seed < 40; ++seed) {
            worldforge::ForgedWorld candidate = worldforge::forgeWorld(seed, 129, config, worldforge::ForgePhase::Politics);
            const MapLayerStack stack(sampleImage(128), candidate);
            if (stack.hasLayer(MapLayer::Biomes) && stack.hasLayer(MapLayer::Roads) && stack.hasLayer(MapLayer::Territories) && stack.hasLayer(MapLayer::Rivers)) {
                return candidate;
            }
        }
        FAIL("no seed forged a world with every layer");
        return worldforge::forgeWorld(1, 129, config, worldforge::ForgePhase::Politics);
    }();
    return world;
}


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
};

}

TEST_CASE("the map draws nothing until shown, then its card and picture")
{
    Fixture f;
    MapScreen map(f.ui);
    REQUIRE(map.ok());
    REQUIRE_FALSE(map.visible());
    map.setImage(sampleImage(8));
    frame(f.ui, f.renderer, 0.0);
    REQUIRE(f.renderer.draws().empty());

    map.setVisible(true);
    frame(f.ui, f.renderer, 0.1);
    REQUIRE_FALSE(f.renderer.draws().empty());
    REQUIRE(drewWhite(f.renderer)); // the picture is the one white-tinted quad

    map.setVisible(false);
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(f.renderer.draws().empty());
}

TEST_CASE("a map with no picture says so and draws no picture")
{
    Fixture f;
    MapScreen map(f.ui);
    map.setVisible(true);
    frame(f.ui, f.renderer, 0.0);
    const std::size_t withoutPicture = f.renderer.draws().size();
    REQUIRE(withoutPicture > 0); // the card and its words
    REQUIRE_FALSE(drewWhite(f.renderer));

    map.setImage(MapImage{});
    map.setImage(sampleImage(0));
    MapImage broken = sampleImage(4);
    broken.rgba.resize(3);
    map.setImage(broken);
    frame(f.ui, f.renderer, 0.1);
    REQUIRE_FALSE(drewWhite(f.renderer));

    map.setImage(sampleImage(1)); // a one pixel world is still a picture
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(drewWhite(f.renderer));
    map.setImage(MapImage{});
    frame(f.ui, f.renderer, 0.3);
    REQUIRE_FALSE(drewWhite(f.renderer));
}

TEST_CASE("the marker sits where the player is, held inside the picture, and a position that is no number is the middle")
{
    Fixture f;
    MapScreen map(f.ui);
    map.setPlayer({0.25f, 0.75f}, 0.0f);
    REQUIRE(map.markerPoint() == MapPoint{0.25f, 0.75f});
    map.setPlayer({-3.0f, 7.0f}, 0.0f);
    REQUIRE(map.markerPoint() == MapPoint{0.0f, 1.0f});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    map.setPlayer({nan, 0.1f}, 0.0f);
    REQUIRE(map.markerPoint() == MapPoint{0.5f, 0.1f});
    map.setPlayer({inf, -inf}, 0.0f);
    REQUIRE(map.markerPoint() == MapPoint{0.5f, 0.5f});
}

TEST_CASE("the heading arrow turns clockwise from north and wraps, and a heading that is no number is north")
{
    Fixture f;
    MapScreen map(f.ui);
    const float quarter = 1.57079632679f;
    map.setPlayer({}, quarter);
    REQUIRE(map.markerDegrees() == Approx(90.0f).margin(0.01));
    map.setPlayer({}, -quarter);
    REQUIRE(map.markerDegrees() == Approx(270.0f).margin(0.01));
    map.setPlayer({}, 4.0f * 3.14159265f);
    REQUIRE(map.markerDegrees() < 0.01f);
    map.setPlayer({}, std::numeric_limits<float>::quiet_NaN());
    REQUIRE(map.markerDegrees() == 0.0f);
    map.setPlayer({}, quarter);
    map.setPlayer({}, std::numeric_limits<float>::infinity());
    REQUIRE(map.markerDegrees() == 0.0f);
    map.setPlayer({}, -1e-30f);
    REQUIRE(map.markerDegrees() < 360.0f);
}

TEST_CASE("moving the marker while shown keeps drawing, and a steady one costs no relayout")
{
    Fixture f;
    MapScreen map(f.ui);
    map.setImage(sampleImage(16));
    map.setVisible(true);
    map.setPlayer({0.2f, 0.2f}, 0.5f);
    frame(f.ui, f.renderer, 0.0);
    const std::size_t first = f.renderer.draws().size();
    map.setPlayer({0.8f, 0.4f}, 2.0f);
    frame(f.ui, f.renderer, 0.1);
    REQUIRE(f.renderer.draws().size() == first);
    const float moved = map.markerPoint().u;
    REQUIRE(moved == Approx(0.8f));
    map.setPlayer({0.8f, 0.4f}, 2.0f);
    REQUIRE(map.markerPoint().u == moved);
    REQUIRE(f.renderer.liveGeometry() > 0);
}

TEST_CASE("clicking the map's close button asks for it to be closed, and clicking elsewhere does not")
{
    Fixture f;
    MapScreen map(f.ui);
    REQUIRE_FALSE(map.closeButtonCentre().has_value());
    map.setVisible(true);
    frame(f.ui, f.renderer, 0.0);
    const auto centre = map.closeButtonCentre();
    REQUIRE(centre.has_value());

    f.ui.mouseMove(640, 400);
    frame(f.ui, f.renderer, 0.1);
    f.ui.mouseButton(0, true);
    f.ui.mouseButton(0, false);
    frame(f.ui, f.renderer, 0.2);
    REQUIRE_FALSE(map.takeCloseRequest());

    f.ui.mouseMove(static_cast<int>(centre->first), static_cast<int>(centre->second));
    frame(f.ui, f.renderer, 0.3);
    f.ui.mouseButton(0, true);
    frame(f.ui, f.renderer, 0.4);
    f.ui.mouseButton(0, false);
    frame(f.ui, f.renderer, 0.5);
    REQUIRE(map.takeCloseRequest());
    REQUIRE_FALSE(map.takeCloseRequest());
}

TEST_CASE("a map screen can go while the context still lives, and another can take its place")
{
    Fixture f;
    {
        MapScreen map(f.ui);
        map.setImage(sampleImage(8));
        map.setVisible(true);
        frame(f.ui, f.renderer, 0.0);
    }
    frame(f.ui, f.renderer, 0.1);
    REQUIRE(f.renderer.draws().empty());
    MapScreen again(f.ui);
    REQUIRE(again.ok());
    again.setImage(sampleImage(8));
    again.setVisible(true);
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(drewWhite(f.renderer));
}

TEST_CASE("toggling a map layer changes the picture that is drawn, and toggling it back restores it")
{
    Fixture f;
    MapScreen map(f.ui);
    map.setLayerStack(MapLayerStack(sampleImage(128), layerWorld()));
    map.setVisible(true);
    frame(f.ui, f.renderer, 0.0);
    const std::uint64_t plain = pictureHash(f.renderer);
    REQUIRE(plain != 0);

    for (const MapLayer layer : {MapLayer::Biomes, MapLayer::Roads, MapLayer::Territories, MapLayer::Rivers}) {
        INFO(mapLayerName(layer));
        map.toggleLayer(layer);
        REQUIRE(map.layers().on(layer));
        frame(f.ui, f.renderer, 0.1);
        REQUIRE(pictureHash(f.renderer) != plain);
        map.toggleLayer(layer);
        frame(f.ui, f.renderer, 0.2);
        REQUIRE(pictureHash(f.renderer) == plain);
    }
}

TEST_CASE("a map without a layer stack draws as it did and shows no layer buttons")
{
    Fixture f;
    MapScreen map(f.ui);
    map.setImage(sampleImage(16));
    map.setVisible(true);
    map.setLayer(MapLayer::Biomes, true);
    frame(f.ui, f.renderer, 0.0);
    const std::uint64_t plain = pictureHash(f.renderer);
    REQUIRE(plain != 0);
    REQUIRE_FALSE(map.layerButtonCentre(MapLayer::Biomes).has_value());

    map.setLayerStack(MapLayerStack(sampleImage(16), layerWorld()));
    frame(f.ui, f.renderer, 0.1);
    REQUIRE(pictureHash(f.renderer) != plain); // the layer set before the stack came is already on
    map.setLayerStack(MapLayerStack{});
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(pictureHash(f.renderer) == plain);
}

TEST_CASE("clicking a layer's button toggles that layer")
{
    Fixture f;
    MapScreen map(f.ui);
    map.setLayerStack(MapLayerStack(sampleImage(128), layerWorld()));
    map.setVisible(true);
    frame(f.ui, f.renderer, 0.0);
    const std::uint64_t plain = pictureHash(f.renderer);
    const auto centre = map.layerButtonCentre(MapLayer::Rivers);
    REQUIRE(centre.has_value());

    f.ui.mouseMove(static_cast<int>(centre->first), static_cast<int>(centre->second));
    frame(f.ui, f.renderer, 0.1);
    f.ui.mouseButton(0, true);
    frame(f.ui, f.renderer, 0.2);
    f.ui.mouseButton(0, false);
    frame(f.ui, f.renderer, 0.3);
    REQUIRE(map.layers().on(MapLayer::Rivers));
    REQUIRE_FALSE(map.layers().on(MapLayer::Roads));
    REQUIRE(pictureHash(f.renderer) != plain);
}

TEST_CASE("a new picture drops the old layers, and the layers survive a hidden map")
{
    Fixture f;
    MapScreen map(f.ui);
    map.setLayerStack(MapLayerStack(sampleImage(64), layerWorld()));
    map.toggleLayer(MapLayer::Biomes);
    map.setVisible(true);
    frame(f.ui, f.renderer, 0.0);
    const std::uint64_t layered = pictureHash(f.renderer);
    map.setVisible(false);
    map.setVisible(true);
    frame(f.ui, f.renderer, 0.1);
    REQUIRE(pictureHash(f.renderer) == layered);

    map.setImage(sampleImage(64));
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(pictureHash(f.renderer) != layered);
    REQUIRE_FALSE(map.layerButtonCentre(MapLayer::Biomes).has_value());
}

TEST_CASE("fog darkens the picture, and a settlement shows with its name only once its ground is seen")
{
    Fixture f;
    MapScreen map(f.ui);
    map.setLayerStack(MapLayerStack(sampleImage(128), layerWorld()));
    map.setVisible(true);
    frame(f.ui, f.renderer, 0.0);
    const std::uint64_t clear = pictureHash(f.renderer);

    map.setSettlements({{"settlement_0001", "Brenor", 3, {0.25f, 0.25f}}, {"settlement_0002", "Kamoth", 0, {0.75f, 0.75f}}});
    REQUIRE(map.placeNames() == std::vector<std::string>{"Brenor", "Kamoth"});

    MapDiscovery fog;
    map.setDiscovery(fog);
    frame(f.ui, f.renderer, 0.1);
    REQUIRE(pictureHash(f.renderer) != clear);
    REQUIRE(map.placeNames().empty());

    fog.reveal({0.25f, 0.25f}, 0.05f);
    map.setDiscovery(fog);
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(map.placeNames() == std::vector<std::string>{"Brenor"});

    fog.reveal({0.75f, 0.75f}, 0.05f);
    map.setDiscovery(fog);
    REQUIRE(map.placeNames().size() == 2);

    map.clearDiscovery();
    frame(f.ui, f.renderer, 0.3);
    REQUIRE(pictureHash(f.renderer) == clear);
}
