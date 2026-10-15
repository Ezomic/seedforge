#include "ichor/traversal/diving.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;

namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

CapabilitySet diver()
{
    CapabilitySet set;
    set.add(Capability::Dive);
    return set;
}
}

TEST_CASE("depth is gated by gear, boundary depths are inclusive", "[diving]")
{
    const auto caps = diver();
    for (const auto gear : {DiveGear::Bare, DiveGear::Breath, DiveGear::Helmet, DiveGear::PressureSuit, DiveGear::Orichalcum}) {
        const float limit = gearProfile(gear).maxDepth;
        CHECK(checkDepth(limit, gear, caps) == DepthCheck::Reachable);
        CHECK(checkDepth(std::nextafter(limit, 1e9f), gear, caps) != DepthCheck::Reachable);
    }
}

TEST_CASE("deeper water needs strictly more gear", "[diving]")
{
    float previous = -1.0f;
    for (const auto gear : {DiveGear::Bare, DiveGear::Breath, DiveGear::Helmet, DiveGear::PressureSuit, DiveGear::Orichalcum}) {
        CHECK(gearProfile(gear).maxDepth > previous);
        previous = gearProfile(gear).maxDepth;
    }
    CHECK(minimumGear(0.0f) == DiveGear::Bare);
    CHECK(minimumGear(gearProfile(DiveGear::Bare).maxDepth) == DiveGear::Bare);
    CHECK(minimumGear(gearProfile(DiveGear::Bare).maxDepth + 0.5f) == DiveGear::Breath);
    CHECK(minimumGear(gearProfile(DiveGear::Helmet).maxDepth + 0.5f) == DiveGear::PressureSuit);
    CHECK(minimumGear(gearProfile(DiveGear::Orichalcum).maxDepth) == DiveGear::Orichalcum);
    CHECK_FALSE(minimumGear(gearProfile(DiveGear::Orichalcum).maxDepth + 1.0f).has_value());
}

TEST_CASE("without the dive capability only bare swimming depth is reachable", "[diving]")
{
    const CapabilitySet none;
    const float bare = gearProfile(DiveGear::Bare).maxDepth;
    CHECK(checkDepth(bare, DiveGear::Orichalcum, none) == DepthCheck::Reachable);
    CHECK(checkDepth(bare + 0.5f, DiveGear::Orichalcum, none) == DepthCheck::NeedsDive);
    CHECK(checkDepth(bare + 0.5f, DiveGear::Orichalcum, diver()) == DepthCheck::Reachable);
    CHECK(deepestReachable(DiveGear::Orichalcum, none) == bare);
    CHECK(deepestReachable(DiveGear::Orichalcum, diver()) == gearProfile(DiveGear::Orichalcum).maxDepth);
}

TEST_CASE("too deep outranks missing capability", "[diving]")
{
    CHECK(checkDepth(10000.0f, DiveGear::Bare, CapabilitySet{}) == DepthCheck::TooDeep);
}

TEST_CASE("NaN, negative and extreme depths are handled", "[diving]")
{
    const auto caps = diver();
    CHECK(checkDepth(kNaN, DiveGear::Bare, CapabilitySet{}) == DepthCheck::Reachable);
    CHECK(checkDepth(-5.0f, DiveGear::Bare, CapabilitySet{}) == DepthCheck::Reachable);
    CHECK(checkDepth(-kInf, DiveGear::Bare, CapabilitySet{}) == DepthCheck::Reachable);
    CHECK(checkDepth(kInf, DiveGear::Orichalcum, caps) == DepthCheck::TooDeep);
    CHECK(checkDepth(std::numeric_limits<float>::max(), DiveGear::Orichalcum, caps) == DepthCheck::TooDeep);
    CHECK(minimumGear(kNaN) == DiveGear::Bare);
    CHECK_FALSE(minimumGear(kInf).has_value());
}

TEST_CASE("an out of range gear value reads as bare", "[diving]")
{
    CHECK(gearProfile(static_cast<DiveGear>(200)).maxDepth == gearProfile(DiveGear::Bare).maxDepth);
    CHECK(checkDepth(30.0f, static_cast<DiveGear>(200), diver()) == DepthCheck::TooDeep);
}

TEST_CASE("pressure makes air run out faster the deeper you are", "[diving]")
{
    CHECK(airDrainPermille(0.0f) == 0);
    CHECK(airDrainPermille(kNaN) == 0);
    CHECK(airDrainPermille(-3.0f) == 0);
    const int shallow = airDrainPermille(5.0f);
    const int mid = airDrainPermille(50.0f);
    CHECK(shallow > 1000);
    CHECK(mid > shallow);
    CHECK(airDrainPermille(kInf) >= airDrainPermille(1.0e9f));
}

TEST_CASE("air remaining falls with dwell time and depth", "[diving]")
{
    const int full = gearProfile(DiveGear::Helmet).airMs;
    CHECK(airRemainingMs(DiveGear::Helmet, 10.0f, 0.0f) == full);
    const int shallowLeft = airRemainingMs(DiveGear::Helmet, 5.0f, 30.0f);
    const int deepLeft = airRemainingMs(DiveGear::Helmet, 30.0f, 30.0f);
    CHECK(shallowLeft < full);
    CHECK(deepLeft < shallowLeft);
    CHECK(airRemainingMs(DiveGear::Helmet, 0.0f, 1000.0f) == full);
}

TEST_CASE("air never goes negative and bad inputs are safe", "[diving]")
{
    CHECK(airRemainingMs(DiveGear::Breath, 10.0f, 1.0e6f) == 0);
    CHECK(airRemainingMs(DiveGear::Breath, 10.0f, kInf) == 0);
    CHECK(airRemainingMs(DiveGear::Breath, 10.0f, kNaN) == gearProfile(DiveGear::Breath).airMs);
    CHECK(airRemainingMs(DiveGear::Breath, 10.0f, -4.0f) == gearProfile(DiveGear::Breath).airMs);
    CHECK(airRemainingMs(DiveGear::Breath, kInf, 1.0f) >= 0);
    CHECK(airRemainingMs(DiveGear::Breath, std::numeric_limits<float>::max(), std::numeric_limits<float>::max()) == 0);
}

TEST_CASE("better gear lasts longer at the same depth", "[diving]")
{
    CHECK(airRemainingMs(DiveGear::Helmet, 10.0f, 20.0f) > airRemainingMs(DiveGear::Breath, 10.0f, 20.0f));
    CHECK(secondsOfAir(DiveGear::Helmet, 10.0f) > secondsOfAir(DiveGear::Breath, 10.0f));
    CHECK(secondsOfAir(DiveGear::Breath, 40.0f) < secondsOfAir(DiveGear::Breath, 5.0f));
}

TEST_CASE("results are deterministic", "[diving]")
{
    CHECK(airRemainingMs(DiveGear::Helmet, 12.3f, 17.7f) == airRemainingMs(DiveGear::Helmet, 12.3f, 17.7f));
    CHECK(airDrainPermille(12.3f) == airDrainPermille(12.3f));
}
