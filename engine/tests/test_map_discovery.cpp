#include "ichor/world/map_discovery.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

MapImage greyImage(std::uint32_t side)
{
    MapImage image;
    image.width = side;
    image.height = side;
    image.rgba.assign(static_cast<std::size_t>(side) * side * 4, 200);
    return image;
}

std::size_t brightPixels(const MapImage& image)
{
    std::size_t bright = 0;
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        bright += image.rgba[i] == 200 ? 1 : 0;
    }
    return bright;
}

constexpr float kStep = 0.005f;

}

TEST_CASE("a new map has seen nothing and the fog covers all of it")
{
    const MapDiscovery fresh;
    REQUIRE(fresh.seenCount() == 0);
    REQUIRE_FALSE(fresh.seen({0.5f, 0.5f}));
    REQUIRE(brightPixels(withFog(greyImage(kMapSide), fresh)) == 0);
}

TEST_CASE("revealing opens a disc of the radius around the player and no more")
{
    MapDiscovery d;
    d.reveal({0.5f, 0.5f}, 0.1f);
    REQUIRE(d.seen({0.5f, 0.5f}));
    REQUIRE(d.seen({0.58f, 0.5f}));
    REQUIRE_FALSE(d.seen({0.65f, 0.5f}));
    REQUIRE_FALSE(d.seen({0.1f, 0.1f}));
    const std::size_t disc = d.seenCount();
    REQUIRE(disc > 400);
    REQUIRE(disc < 700);
    d.reveal({0.5f, 0.5f}, 0.1f);
    REQUIRE(d.seenCount() == disc);
}

TEST_CASE("fog goes only where nothing was seen, and a scripted walk lifts it along its path")
{
    const MapImage grey = greyImage(kMapSide);
    MapDiscovery d;
    const std::size_t before = brightPixels(withFog(grey, d));
    for (float u = 0.1f; u <= 0.9f; u += kStep) {
        d.reveal({u, 0.3f}, 0.04f);
    }
    const MapImage fogged = withFog(grey, d);
    REQUIRE(brightPixels(fogged) > before + 500);
    REQUIRE(brightPixels(fogged) < kMapSide * kMapSide / 3);
    REQUIRE(d.seen({0.5f, 0.3f}));
    REQUIRE_FALSE(d.seen({0.5f, 0.7f}));
    const std::size_t seenAfterWalk = d.seenCount();

    d.reveal({0.5f, 0.3f}, 0.04f);
    REQUIRE(d.seenCount() == seenAfterWalk);
    REQUIRE(d.seen({0.1f, 0.3f}));
}

TEST_CASE("a long stride leaves no gap")
{
    MapDiscovery d;
    d.revealSegment({0.05f, 0.5f}, {0.95f, 0.5f}, 0.02f);
    for (float u = 0.05f; u <= 0.95f; u += 0.01f) {
        REQUIRE(d.seen({u, 0.5f}));
    }
}

TEST_CASE("discovery survives hostile input")
{
    MapDiscovery d;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    d.reveal({nan, 0.5f}, 0.1f);
    d.reveal({0.5f, inf}, 0.1f);
    d.revealSegment({nan, 0.0f}, {0.5f, 0.5f}, 0.1f);
    REQUIRE(d.seenCount() == 0);
    REQUIRE_FALSE(d.seen({nan, nan}));

    d.reveal({0.5f, 0.5f}, nan);
    d.reveal({0.2f, 0.2f}, -3.0f);
    REQUIRE(d.seenCount() == 2);

    d.reveal({-5.0f, 9.0f}, 0.02f);
    REQUIRE(d.seen({0.0f, 1.0f}));

    MapDiscovery all;
    all.reveal({0.5f, 0.5f}, 1.0e30f);
    REQUIRE(all.seenCount() == static_cast<std::size_t>(kMapSide) * kMapSide);

    MapDiscovery tiny(0);
    REQUIRE(tiny.side() == 1);
    MapDiscovery huge(1u << 30);
    REQUIRE(huge.side() <= 4096);
    REQUIRE(withFog(greyImage(8), d).rgba == greyImage(8).rgba);
    REQUIRE(withFog(MapImage{}, d).empty());
}

TEST_CASE("the same walk gives the same discovery")
{
    MapDiscovery a;
    MapDiscovery b;
    a.revealSegment({0.1f, 0.1f}, {0.8f, 0.6f}, 0.03f);
    b.revealSegment({0.1f, 0.1f}, {0.8f, 0.6f}, 0.03f);
    REQUIRE(a == b);
    b.reveal({0.9f, 0.9f}, 0.01f);
    REQUIRE_FALSE(a == b);
}

TEST_CASE("settlement names are stable, readable and different from one another")
{
    REQUIRE(settlementMapName(7, "settlement_0001") == settlementMapName(7, "settlement_0001"));
    REQUIRE(settlementMapName(7, "settlement_0001") != settlementMapName(8, "settlement_0001"));
    std::set<std::string> names;
    for (int i = 0; i < 200; ++i) {
        const std::string name = settlementMapName(3, "settlement_" + std::to_string(i));
        REQUIRE(name.size() >= 4);
        REQUIRE(name[0] >= 'A');
        REQUIRE(name[0] <= 'Z');
        names.insert(name);
    }
    REQUIRE(names.size() > 150);
}

TEST_CASE("map settlements keep the sites' order, tiers and unique names, and put each on the map")
{
    WorldDesc desc;
    desc.sizeMetres = 1000;
    std::vector<SettlementSite> sites;
    for (int i = 0; i < 40; ++i) {
        sites.push_back({"settlement_" + std::to_string(i), static_cast<std::uint8_t>(i % 7), 100.0 * (i % 10), 250.0});
    }
    const std::vector<MapSettlement> places = mapSettlements(desc, 5, sites);
    REQUIRE(places.size() == sites.size());
    std::set<std::string> names;
    for (std::size_t i = 0; i < places.size(); ++i) {
        REQUIRE(places[i].id == sites[i].id);
        REQUIRE(places[i].tier <= 3);
        REQUIRE(places[i].at == mapPosition(desc, sites[i].x, sites[i].z));
        names.insert(places[i].name);
    }
    REQUIRE(names.size() == places.size());
    REQUIRE(mapSettlements(desc, 5, {}).empty());
}
