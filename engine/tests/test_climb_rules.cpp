#include "ichor/traversal/climb_rules.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ichor;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

CapabilitySet climber()
{
    CapabilitySet set;
    set.add(Capability::Climb);
    return set;
}

CapabilitySet descender()
{
    CapabilitySet set;
    set.add(Capability::Descend);
    return set;
}

ClimbSurface wall(float slope, float height)
{
    return {true, slope, height};
}

}

TEST_CASE("a climb on a designated wall within limits is allowed and priced", "[climb]")
{
    const ClimbLimits limits;
    const auto result = assessClimb(limits, climber(), ClimbDirection::Up, wall(50.0f, 10.0f), 100.0f);
    CHECK(result.allowed());
    CHECK(result.staminaCost == 40.0f);
}

TEST_CASE("grip cost doubles from the gentlest to the steepest climbable slope", "[climb]")
{
    const ClimbLimits limits;
    const auto gentle = assessClimb(limits, climber(), ClimbDirection::Up, wall(50.0f, 10.0f), 1000.0f);
    const auto vertical = assessClimb(limits, climber(), ClimbDirection::Up, wall(90.0f, 10.0f), 1000.0f);
    CHECK(vertical.staminaCost == 2.0f * gentle.staminaCost);
}

TEST_CASE("missing capability blocks, and Climb does not grant Descend", "[climb]")
{
    const ClimbLimits limits;
    CHECK(assessClimb(limits, CapabilitySet{}, ClimbDirection::Up, wall(70.0f, 5.0f), 100.0f).block
        == ClimbBlock::NoCapability);
    CHECK(assessClimb(limits, descender(), ClimbDirection::Up, wall(70.0f, 5.0f), 100.0f).block
        == ClimbBlock::NoCapability);
    CHECK(assessClimb(limits, climber(), ClimbDirection::Down, wall(70.0f, 5.0f), 100.0f).block
        == ClimbBlock::NoCapability);
    CHECK(assessClimb(limits, descender(), ClimbDirection::Down, wall(70.0f, 5.0f), 100.0f).allowed());
}

TEST_CASE("free rock is never climbable", "[climb]")
{
    const ClimbLimits limits;
    ClimbSurface rock = wall(70.0f, 5.0f);
    rock.designated = false;
    CHECK(assessClimb(limits, climber(), ClimbDirection::Up, rock, 100.0f).block == ClimbBlock::NotDesignated);
}

TEST_CASE("slope boundaries", "[climb]")
{
    const ClimbLimits limits;
    const auto at = [&](float slope) {
        return assessClimb(limits, climber(), ClimbDirection::Up, wall(slope, 5.0f), 100.0f).block;
    };
    CHECK(at(49.99f) == ClimbBlock::TooGentle);
    CHECK(at(50.0f) == ClimbBlock::None);
    CHECK(at(90.0f) == ClimbBlock::None);
    CHECK(at(90.01f) == ClimbBlock::TooSteep);
    CHECK(at(0.0f) == ClimbBlock::TooGentle);
}

TEST_CASE("height boundaries differ for climb and descent", "[climb]")
{
    const ClimbLimits limits;
    const auto up = [&](float h) {
        return assessClimb(limits, climber(), ClimbDirection::Up, wall(70.0f, h), 1.0e6f).block;
    };
    const auto down = [&](float h) {
        return assessClimb(limits, descender(), ClimbDirection::Down, wall(70.0f, h), 1.0e6f).block;
    };
    CHECK(up(30.0f) == ClimbBlock::None);
    CHECK(up(30.01f) == ClimbBlock::TooHigh);
    CHECK(down(60.0f) == ClimbBlock::None);
    CHECK(down(60.01f) == ClimbBlock::TooHigh);
    CHECK(up(0.0f) == ClimbBlock::None);
}

TEST_CASE("stamina must cover the whole surface", "[climb]")
{
    const ClimbLimits limits;
    const auto surface = wall(50.0f, 10.0f); // costs 40
    CHECK(assessClimb(limits, climber(), ClimbDirection::Up, surface, 40.0f).allowed());
    const auto shortOfStamina = assessClimb(limits, climber(), ClimbDirection::Up, surface, 39.99f);
    CHECK(shortOfStamina.block == ClimbBlock::NoStamina);
    CHECK(shortOfStamina.staminaCost == 40.0f);
    CHECK(assessClimb(limits, climber(), ClimbDirection::Up, surface, 0.0f).block == ClimbBlock::NoStamina);
}

TEST_CASE("descent is cheaper than climbing", "[climb]")
{
    const ClimbLimits limits;
    const auto up = assessClimb(limits, climber(), ClimbDirection::Up, wall(70.0f, 10.0f), 1000.0f);
    const auto down = assessClimb(limits, descender(), ClimbDirection::Down, wall(70.0f, 10.0f), 1000.0f);
    CHECK(down.staminaCost < up.staminaCost);
}

TEST_CASE("NaN, infinite and negative inputs are rejected before any maths", "[climb]")
{
    const ClimbLimits limits;
    const auto check = [&](const ClimbSurface& s, float stamina) {
        const auto r = assessClimb(limits, climber(), ClimbDirection::Up, s, stamina);
        CHECK(r.block == ClimbBlock::InvalidInput);
        CHECK(r.staminaCost == 0.0f);
    };
    check(wall(kNaN, 5.0f), 100.0f);
    check(wall(70.0f, kNaN), 100.0f);
    check(wall(kInf, 5.0f), 100.0f);
    check(wall(70.0f, kInf), 100.0f);
    check(wall(-70.0f, 5.0f), 100.0f);
    check(wall(70.0f, -5.0f), 100.0f);
    check(wall(70.0f, 5.0f), kNaN);
    check(wall(70.0f, 5.0f), -1.0f);
    check(wall(70.0f, 5.0f), kInf);
}

TEST_CASE("invalid limits reject everything", "[climb]")
{
    ClimbLimits bad;
    bad.maxSlopeDegrees = kNaN;
    CHECK_FALSE(bad.valid());
    CHECK(assessClimb(bad, climber(), ClimbDirection::Up, wall(70.0f, 5.0f), 100.0f).block == ClimbBlock::InvalidInput);

    ClimbLimits inverted;
    inverted.minSlopeDegrees = 80.0f;
    inverted.maxSlopeDegrees = 60.0f;
    CHECK_FALSE(inverted.valid());

    ClimbLimits fall;
    fall.safeFallMetres = 20.0f;
    fall.lethalFallMetres = 20.0f;
    CHECK_FALSE(fall.valid());

    ClimbLimits negative;
    negative.climbStaminaPerMetre = -1.0f;
    CHECK_FALSE(negative.valid());

    CHECK(ClimbLimits{}.valid());
}

TEST_CASE("extreme heights do not overflow the stamina price", "[climb]")
{
    ClimbLimits limits;
    limits.maxClimbMetres = 3.0e38f;
    limits.climbStaminaPerMetre = 1.0e30f;
    const auto r = assessClimb(limits, climber(), ClimbDirection::Up, wall(90.0f, 3.0e38f), 1.0e38f);
    CHECK(r.block == ClimbBlock::NoStamina);
    CHECK(r.staminaCost <= std::numeric_limits<float>::max());
    CHECK(r.staminaCost == r.staminaCost);
}

TEST_CASE("fall thresholds", "[climb][fall]")
{
    const ClimbLimits limits;
    CHECK(assessFall(limits, 0.0f).severity == FallSeverity::Safe);
    CHECK(assessFall(limits, 3.0f).severity == FallSeverity::Safe);
    CHECK(assessFall(limits, 3.0f).damageFraction == 0.0f);
    const auto mid = assessFall(limits, 3.01f);
    CHECK(mid.severity == FallSeverity::Injured);
    CHECK(mid.damageFraction > 0.0f);
    const auto half = assessFall(limits, 11.5f);
    CHECK(half.damageFraction > 0.49f);
    CHECK(half.damageFraction < 0.51f);
    CHECK(assessFall(limits, 19.99f).severity == FallSeverity::Injured);
    const auto dead = assessFall(limits, 20.0f);
    CHECK(dead.severity == FallSeverity::Lethal);
    CHECK(dead.damageFraction == 1.0f);
}

TEST_CASE("fall damage rises with height", "[climb][fall]")
{
    const ClimbLimits limits;
    float previous = -1.0f;
    for (float h = 0.0f; h <= 25.0f; h += 0.5f) {
        const float d = assessFall(limits, h).damageFraction;
        CHECK(d >= previous);
        previous = d;
    }
    CHECK(assessFall(limits, 10.0f).damageFraction > assessFall(limits, 5.0f).damageFraction);
}

TEST_CASE("fall inputs that are not numbers or are negative read as zero, infinity is lethal", "[climb][fall]")
{
    const ClimbLimits limits;
    CHECK(assessFall(limits, kNaN).severity == FallSeverity::Safe);
    CHECK(assessFall(limits, -50.0f).severity == FallSeverity::Safe);
    CHECK(assessFall(limits, kInf).severity == FallSeverity::Lethal);
    CHECK(assessFall(limits, 3.0e38f).damageFraction == 1.0f);

    ClimbLimits bad;
    bad.lethalFallMetres = kNaN;
    CHECK(assessFall(bad, 10.0f).severity == FallSeverity::Injured);
    CHECK(assessFall(bad, 25.0f).severity == FallSeverity::Lethal);
}
