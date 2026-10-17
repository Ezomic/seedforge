#include "ichor/ui/map_screen_model.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

MapViewport view200()
{
    return {0.0f, 0.0f, 2.0f, 200.0f, 200.0f};
}

MapAnnotation mark(std::string id, float x, float y)
{
    return {std::move(id), AnnotationKind::Mark, AnnotationSymbol::Camp, "", {{x, y}}};
}

}

TEST_CASE("world to screen puts the centre mid-view with north up", "[map_screen_model]")
{
    const auto c = mapWorldToScreen(view200(), 0.0f, 0.0f);
    REQUIRE(c);
    CHECK(c->x == Approx(100.0f));
    CHECK(c->y == Approx(100.0f));
    const auto n = mapWorldToScreen(view200(), 10.0f, 10.0f);
    CHECK(n->x == Approx(120.0f));
    CHECK(n->y == Approx(80.0f));
}

TEST_CASE("invalid viewports and positions are rejected", "[map_screen_model]")
{
    MapViewport bad = view200();
    bad.zoom = kNaN;
    CHECK_FALSE(isValidMapViewport(bad));
    bad = view200();
    bad.width = 0.0f;
    CHECK_FALSE(isValidMapViewport(bad));
    bad = view200();
    bad.zoom = 1000.0f;
    CHECK_FALSE(isValidMapViewport(bad));
    bad = view200();
    bad.centerX = kInf;
    CHECK_FALSE(isValidMapViewport(bad));
    CHECK_FALSE(mapWorldToScreen(bad, 0.0f, 0.0f));
    CHECK_FALSE(mapWorldToScreen(view200(), kNaN, 0.0f));
    CHECK_FALSE(mapWorldToScreen(view200(), 0.0f, 2.0e6f));
    MapScreenInput in;
    in.viewport = bad;
    CHECK_FALSE(buildMapScreen(in).valid);
}

TEST_CASE("panning follows the drag and clamps", "[map_screen_model]")
{
    const MapViewport p = panMapViewport(view200(), 20.0f, 20.0f);
    CHECK(p.centerX == Approx(-10.0f));
    CHECK(p.centerY == Approx(10.0f));
    CHECK(panMapViewport(view200(), kNaN, 1.0f).centerX == 0.0f);
    MapViewport edge = view200();
    edge.centerX = 999999.0f;
    CHECK(panMapViewport(edge, -1.0e9f, 0.0f).centerX == kMapViewMaxCoordinate);
}

TEST_CASE("zooming keeps the anchor fixed and clamps the zoom", "[map_screen_model]")
{
    const MapViewport v = view200();
    const MapScreenPoint anchor = *mapWorldToScreen(v, 15.0f, -5.0f);
    const MapViewport z = zoomMapViewport(v, 2.0f, anchor.x, anchor.y);
    CHECK(z.zoom == Approx(4.0f));
    const auto after = mapWorldToScreen(z, 15.0f, -5.0f);
    CHECK(after->x == Approx(anchor.x).margin(0.01));
    CHECK(after->y == Approx(anchor.y).margin(0.01));
    CHECK(zoomMapViewport(v, 1.0e9f, 0.0f, 0.0f).zoom == kMapViewMaxZoom);
    CHECK(zoomMapViewport(v, 1.0e-9f, 0.0f, 0.0f).zoom == kMapViewMinZoom);
    CHECK(zoomMapViewport(v, 0.0f, 0.0f, 0.0f).zoom == 2.0f);
    CHECK(zoomMapViewport(v, -2.0f, 0.0f, 0.0f).zoom == 2.0f);
    CHECK(zoomMapViewport(v, kNaN, 0.0f, 0.0f).zoom == 2.0f);
    CHECK(zoomMapViewport(v, 2.0f, kInf, 0.0f).zoom == 2.0f);
    const MapViewport huge = zoomMapViewport(v, 2.0f, 3.0e38f, -3.0e38f);
    CHECK(isValidMapViewport(huge));
    CHECK(huge.centerX == kMapViewMaxCoordinate);
    CHECK(huge.centerY == kMapViewMaxCoordinate);
}

TEST_CASE("centering moves the view onto a point", "[map_screen_model]")
{
    const MapViewport c = centerMapViewport(view200(), 30.0f, -40.0f);
    const auto s = mapWorldToScreen(c, 30.0f, -40.0f);
    CHECK(s->x == Approx(100.0f));
    CHECK(s->y == Approx(100.0f));
    CHECK(centerMapViewport(view200(), kNaN, 0.0f).centerX == 0.0f);
}

TEST_CASE("only annotations touching the view are listed", "[map_screen_model]")
{
    MapAnnotationStore store;
    REQUIRE(store.add(mark("in", 10.0f, 10.0f)) == AnnotationResult::Ok);
    REQUIRE(store.add(mark("out", 500.0f, 0.0f)) == AnnotationResult::Ok);
    REQUIRE(store.add(mark("edge", 50.0f, 0.0f)) == AnnotationResult::Ok);
    MapScreenInput in;
    in.viewport = view200();
    in.annotations = &store;
    const MapScreenView v = buildMapScreen(in);
    REQUIRE(v.valid);
    REQUIRE(v.annotations.size() == 2);
    CHECK(v.annotations[0].id == "in");
    CHECK(v.annotations[1].id == "edge");
    CHECK(v.hiddenAnnotations == 1);

    in.viewport = centerMapViewport(in.viewport, 500.0f, 0.0f);
    const MapScreenView moved = buildMapScreen(in);
    REQUIRE(moved.annotations.size() == 1);
    CHECK(moved.annotations[0].id == "out");
    CHECK(moved.annotations[0].points[0].x == Approx(100.0f));
}

TEST_CASE("a path crossing the view is kept though both ends are outside", "[map_screen_model]")
{
    MapAnnotationStore store;
    MapAnnotation path{"p", AnnotationKind::Path, AnnotationSymbol::Direction, "", {{-500.0f, 0.0f}, {500.0f, 0.0f}}};
    REQUIRE(store.add(path) == AnnotationResult::Ok);
    MapAnnotation away{"q", AnnotationKind::Path, AnnotationSymbol::Direction, "", {{-500.0f, 400.0f}, {500.0f, 400.0f}}};
    REQUIRE(store.add(away) == AnnotationResult::Ok);
    MapScreenInput in;
    in.viewport = view200();
    in.annotations = &store;
    const MapScreenView v = buildMapScreen(in);
    REQUIRE(v.annotations.size() == 1);
    CHECK(v.annotations[0].id == "p");
    CHECK(v.annotations[0].points.size() == 2);
    CHECK(v.hiddenAnnotations == 1);
}

TEST_CASE("the player marker tracks position and heading", "[map_screen_model]")
{
    MapScreenInput in;
    in.viewport = view200();
    CHECK_FALSE(buildMapScreen(in).player);
    in.player = WaypointPosition{20.0f, 0.0f};
    in.playerHeading = 1.5f;
    auto v = buildMapScreen(in);
    REQUIRE(v.player);
    CHECK(v.player->position.x == Approx(140.0f));
    CHECK(v.player->onScreen);
    CHECK(v.player->heading == Approx(1.5f));

    in.player = WaypointPosition{900.0f, 0.0f};
    in.playerHeading = kNaN;
    v = buildMapScreen(in);
    REQUIRE(v.player);
    CHECK_FALSE(v.player->onScreen);
    CHECK(v.player->heading == 0.0f);

    in.player = WaypointPosition{kNaN, 0.0f};
    CHECK_FALSE(buildMapScreen(in).player);
}

TEST_CASE("the waypoint shows only when enabled and clamps to the edge off screen", "[map_screen_model]")
{
    WaypointSet set;
    REQUIRE(set.add({"home", {0.0f, 1000.0f}}) == WaypointResult::Ok);
    MapScreenInput in;
    in.viewport = view200();
    in.player = WaypointPosition{0.0f, 0.0f};
    in.waypoints = &set;

    CHECK_FALSE(buildMapScreen(in).waypoint);

    in.waypointSettings.enabled = true;
    const MapScreenView v = buildMapScreen(in);
    REQUIRE(v.waypoint);
    CHECK(v.waypoint->id == "home");
    CHECK_FALSE(v.waypoint->onScreen);
    CHECK(v.waypoint->edge.y == 0.0f);
    CHECK(v.waypoint->edge.x == Approx(100.0f));
    CHECK(v.waypoint->distance == Approx(1000.0f));

    in.player.reset();
    CHECK_FALSE(buildMapScreen(in).waypoint);

    WaypointSet empty;
    in.player = WaypointPosition{0.0f, 0.0f};
    in.waypoints = &empty;
    CHECK_FALSE(buildMapScreen(in).waypoint);
}

TEST_CASE("missing sources give an empty but valid view", "[map_screen_model]")
{
    MapScreenInput in;
    in.viewport = view200();
    const MapScreenView v = buildMapScreen(in);
    CHECK(v.valid);
    CHECK(v.annotations.empty());
    CHECK(v.hiddenAnnotations == 0);
    CHECK_FALSE(v.player);
    CHECK_FALSE(v.waypoint);
}
