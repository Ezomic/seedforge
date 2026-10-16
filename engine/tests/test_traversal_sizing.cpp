#include "ichor/traversal/traversal_sizing.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ichor;

namespace {

TraversalSet onFoot() { return TraversalSet{}; }

TraversalSet mounted()
{
    TraversalSet s;
    s.mountSpeed = 9.0F;
    s.mountedShare = 700;
    return s;
}

}

TEST_CASE("the documented run speed gives the documented 4 km world", "[traversal_sizing]")
{
    const WorldSizing w = sizeWorld(onFoot());
    REQUIRE(w.status == SizingStatus::Ok);
    REQUIRE(w.effectiveSpeedMmPerS == 4000);
    REQUIRE(w.worldEdgeMetres == 4000);
    REQUIRE(w.glanceSpacingMetres == 300);
    REQUIRE(w.siteSpacingMetres == 960);
    REQUIRE(w.regionSpanMetres == 2400);
    REQUIRE(w.regionsPerEdge == 2);
    REQUIRE(sizingWithinBounds(w));
}

TEST_CASE("a no-mount world is smaller and denser than a mounted one", "[traversal_sizing]")
{
    const WorldSizing slow = sizeWorld(onFoot());
    const WorldSizing fast = sizeWorld(mounted());
    REQUIRE(fast.worldEdgeMetres > slow.worldEdgeMetres);
    REQUIRE(fast.siteSpacingMetres > slow.siteSpacingMetres);
    REQUIRE(fast.regionSpanMetres > slow.regionSpanMetres);
}

TEST_CASE("every traversal knob moves the world size", "[traversal_sizing]")
{
    TraversalSet boats = onFoot();
    boats.boatSpeed = 8.0F;
    boats.boatShare = 500;
    TraversalSet fastTravel = onFoot();
    fastTravel.fastTravel = true;
    TraversalSet slowFoot = onFoot();
    slowFoot.footSpeed = 2.5F;

    const std::uint32_t base = sizeWorld(onFoot()).worldEdgeMetres;
    REQUIRE(sizeWorld(boats).worldEdgeMetres > base);
    REQUIRE(sizeWorld(fastTravel).worldEdgeMetres == base * 1250 / 1000);
    REQUIRE(sizeWorld(slowFoot).worldEdgeMetres < base);
    REQUIRE(sizeWorld(mounted()).worldEdgeMetres > base);
}

TEST_CASE("an unused mount or boat changes nothing", "[traversal_sizing]")
{
    TraversalSet s = onFoot();
    s.mountSpeed = 12.0F;
    s.boatSpeed = 12.0F;
    REQUIRE(sizeWorld(s).worldEdgeMetres == sizeWorld(onFoot()).worldEdgeMetres);
    TraversalSet noMount = onFoot();
    noMount.mountedShare = 900;
    REQUIRE(sizeWorld(noMount).worldEdgeMetres == sizeWorld(onFoot()).worldEdgeMetres);
}

TEST_CASE("the blend is time weighted, not a speed average", "[traversal_sizing]")
{
    TraversalSet s = onFoot();
    s.mountSpeed = 12.0F;
    s.mountedShare = 500;
    // Half the distance at 4 m/s and half at 12 m/s takes the time of 6 m/s, not 8 m/s.
    REQUIRE(sizeWorld(s).effectiveSpeedMmPerS == 6000);
}

TEST_CASE("shares beyond the whole are scaled back", "[traversal_sizing]")
{
    TraversalSet s = onFoot();
    s.mountSpeed = 8.0F;
    s.boatSpeed = 8.0F;
    s.mountedShare = 800;
    s.boatShare = 800;
    REQUIRE(sizeWorld(s).effectiveSpeedMmPerS == 8000);
    s.mountedShare = std::numeric_limits<std::uint32_t>::max();
    s.boatShare = std::numeric_limits<std::uint32_t>::max();
    REQUIRE(sizeWorld(s).effectiveSpeedMmPerS == 8000);
}

TEST_CASE("the edge is clamped at both ends", "[traversal_sizing]")
{
    TraversalSet crawl = onFoot();
    crawl.footSpeed = 0.01F;
    const WorldSizing small = sizeWorld(crawl);
    REQUIRE(small.worldEdgeMetres == kSizingMinEdgeMetres);
    REQUIRE(small.edgeClamped);
    REQUIRE(small.regionSpanMetres <= small.worldEdgeMetres);
    REQUIRE(small.regionsPerEdge >= 1);

    TraversalSet rocket = onFoot();
    rocket.footSpeed = 1.0e9F;
    rocket.fastTravel = true;
    const WorldSizing big = sizeWorld(rocket);
    REQUIRE(big.worldEdgeMetres == kSizingMaxEdgeMetres);
    REQUIRE(big.edgeClamped);
    REQUIRE(big.effectiveSpeedMmPerS == 100000);
}

TEST_CASE("bad speeds are reported and never poison the result", "[traversal_sizing]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const WorldSizing base = sizeWorld(onFoot());

    for (const float bad : {nan, inf, -inf, -1.0F, 0.0F}) {
        TraversalSet s = onFoot();
        s.footSpeed = bad;
        const WorldSizing w = sizeWorld(s);
        REQUIRE(w.status == SizingStatus::InvalidSpeed);
        REQUIRE(w.worldEdgeMetres == base.worldEdgeMetres);
    }
    for (const float bad : {nan, inf, -inf, -3.0F}) {
        TraversalSet s = mounted();
        s.mountSpeed = bad;
        s.boatSpeed = bad;
        const WorldSizing w = sizeWorld(s);
        REQUIRE(w.status == SizingStatus::InvalidSpeed);
        REQUIRE(w.worldEdgeMetres == base.worldEdgeMetres);
    }
}

TEST_CASE("sizing is deterministic and the bounds check can fail", "[traversal_sizing]")
{
    REQUIRE(sizeWorld(mounted()).worldEdgeMetres == sizeWorld(mounted()).worldEdgeMetres);

    WorldSizing w = sizeWorld(onFoot());
    w.siteSpacingMetres *= 2;
    REQUIRE_FALSE(sizingWithinBounds(w));
    w = sizeWorld(onFoot());
    w.regionSpanMetres *= 2;
    REQUIRE_FALSE(sizingWithinBounds(w));
}

TEST_CASE("travel seconds handles zero speed and large distances", "[traversal_sizing]")
{
    REQUIRE(travelSeconds(1000, 0) == 0);
    REQUIRE(travelSeconds(4000, 4000) == 1000);
    REQUIRE(travelSeconds(std::numeric_limits<std::uint32_t>::max(), 100) > 0);
}
