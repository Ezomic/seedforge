#include "ichor/world/map_layers.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

using namespace ichor;

namespace {

constexpr int kGrid = 129;
constexpr std::uint32_t kSide = 128;

MapImage greyImage(std::uint32_t side)
{
    MapImage image;
    image.width = side;
    image.height = side;
    image.rgba.assign(static_cast<std::size_t>(side) * side * 4, 128);
    return image;
}

std::size_t changedPixels(const MapImage& a, const MapImage& b)
{
    REQUIRE(a.rgba.size() == b.rgba.size());
    std::size_t changed = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        if (a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] || a.rgba[i + 2] != b.rgba[i + 2]) {
            ++changed;
        }
    }
    return changed;
}

MapLayerSet only(MapLayer layer)
{
    MapLayerSet set;
    set.set(layer, true);
    return set;
}

constexpr MapLayer kAllLayers[] = {MapLayer::Biomes, MapLayer::Roads, MapLayer::Territories, MapLayer::Rivers};

worldforge::ForgeConfig twoFactions()
{
    worldforge::FactionSpec a;
    worldforge::FactionSpec b;
    b.strategy = worldforge::ClaimStrategy::ResourceFollowing;
    b.oreWeight = 200;
    worldforge::ForgeConfig config;
    config.factions = {a, b};
    config.hydrology.riverThreshold = 48;
    return config;
}

const worldforge::ForgedWorld& fullWorld()
{
    static const worldforge::ForgedWorld world = [] {
        for (std::uint64_t seed = 1; seed < 40; ++seed) {
            worldforge::ForgedWorld candidate = worldforge::forgeWorld(seed, kGrid, twoFactions(), worldforge::ForgePhase::Politics);
            if (candidate.roads() && !candidate.roads()->edges.empty() && candidate.politics()) {
                return candidate;
            }
        }
        FAIL("no seed forged a world with roads and politics");
        return worldforge::forgeWorld(1, kGrid, twoFactions(), worldforge::ForgePhase::Politics);
    }();
    return world;
}

}

TEST_CASE("with no layer on, the composite is the plain picture")
{
    const MapLayerStack stack(greyImage(kSide), fullWorld());
    REQUIRE_FALSE(stack.empty());
    REQUIRE(stack.composite(MapLayerSet{}).rgba == stack.base().rgba);
}

TEST_CASE("each layer on changes the picture, and the biome layer covers most of it")
{
    const MapLayerStack stack(greyImage(kSide), fullWorld());
    for (const MapLayer layer : kAllLayers) {
        INFO(mapLayerName(layer));
        REQUIRE(stack.hasLayer(layer));
        REQUIRE(changedPixels(stack.composite(only(layer)), stack.base()) > 0);
    }
    REQUIRE(changedPixels(stack.composite(only(MapLayer::Biomes)), stack.base()) > kSide * kSide / 2);
    REQUIRE(changedPixels(stack.composite(only(MapLayer::Roads)), stack.base()) < kSide * kSide / 4);
    REQUIRE(changedPixels(stack.composite(only(MapLayer::Rivers)), stack.base()) < kSide * kSide / 4);
}

TEST_CASE("layers are independent and stack in order")
{
    const MapLayerStack stack(greyImage(kSide), fullWorld());
    MapLayerSet all;
    for (const MapLayer layer : kAllLayers) {
        all.set(layer, true);
    }
    const MapImage everything = stack.composite(all);
    for (const MapLayer layer : kAllLayers) {
        INFO(mapLayerName(layer));
        MapLayerSet without = all;
        without.toggle(layer);
        REQUIRE(stack.composite(without).rgba != everything.rgba);
    }
    MapLayerSet toggled;
    toggled.toggle(MapLayer::Roads);
    REQUIRE(toggled.on(MapLayer::Roads));
    REQUIRE_FALSE(toggled.on(MapLayer::Rivers));
    toggled.toggle(MapLayer::Roads);
    REQUIRE(toggled == MapLayerSet{});
}

TEST_CASE("a road is drawn opaque where the forge put one")
{
    const MapLayerStack stack(greyImage(kSide), fullWorld());
    const MapImage roads = stack.composite(only(MapLayer::Roads));
    std::size_t brown = 0;
    for (std::size_t i = 0; i + 3 < roads.rgba.size(); i += 4) {
        if (roads.rgba[i] == 92 && roads.rgba[i + 1] == 52 && roads.rgba[i + 2] == 20) {
            ++brown;
        }
    }
    REQUIRE(brown > 0);
}

TEST_CASE("a world that stopped early has no layers past its phase, and they change nothing")
{
    worldforge::ForgeConfig config;
    config.hydrology.riverThreshold = 48;
    const worldforge::ForgedWorld early = worldforge::forgeWorld(7, kGrid, config, worldforge::ForgePhase::Hydrology);
    const MapLayerStack stack(greyImage(kSide), early);
    REQUIRE(stack.hasLayer(MapLayer::Rivers));
    REQUIRE_FALSE(stack.hasLayer(MapLayer::Roads));
    REQUIRE_FALSE(stack.hasLayer(MapLayer::Territories));
    REQUIRE_FALSE(stack.hasLayer(MapLayer::Biomes));
    MapLayerSet noRivers;
    noRivers.set(MapLayer::Roads, true);
    noRivers.set(MapLayer::Territories, true);
    noRivers.set(MapLayer::Biomes, true);
    REQUIRE(stack.composite(noRivers).rgba == stack.base().rgba);
}

TEST_CASE("an empty or non-square picture gives an empty stack, and a picture smaller than the grid keeps its thin features")
{
    REQUIRE(MapLayerStack(MapImage{}, fullWorld()).empty());
    REQUIRE(MapLayerStack(MapImage{}, fullWorld()).composite(only(MapLayer::Biomes)).empty());
    MapImage lopsided = greyImage(8);
    lopsided.height = 4;
    lopsided.rgba.resize(8 * 4 * 4);
    REQUIRE(MapLayerStack(lopsided, fullWorld()).empty());
    MapImage broken = greyImage(8);
    broken.rgba.resize(5);
    REQUIRE(MapLayerStack(broken, fullWorld()).empty());

    const MapLayerStack small(greyImage(16), fullWorld());
    REQUIRE(small.hasLayer(MapLayer::Rivers));
    REQUIRE(small.hasLayer(MapLayer::Roads));
    const MapLayerStack tiny(greyImage(1), fullWorld());
    REQUIRE_FALSE(tiny.empty());
    REQUIRE(tiny.composite(only(MapLayer::Biomes)).rgba.size() == 4);
}

TEST_CASE("a world the forge refused gives a stack with no layers")
{
    const worldforge::ForgedWorld none = worldforge::forgeWorld(1, 1);
    REQUIRE_FALSE(none.hydrology().has_value());
    const MapLayerStack stack(greyImage(kSide), none);
    for (const MapLayer layer : kAllLayers) {
        REQUIRE_FALSE(stack.hasLayer(layer));
    }
    REQUIRE(stack.composite(only(MapLayer::Biomes)).rgba == stack.base().rgba);
}
