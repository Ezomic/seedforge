#include "ichor/ui/combat_hud.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;
using Catch::Approx;

namespace {

CombatHudInput base()
{
    CombatHudInput input;
    input.health = 50.0f;
    input.maxHealth = 100.0f;
    input.stamina = 80.0f;
    input.maxStamina = 100.0f;
    return input;
}

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kHalfPi = 1.5707964f;
constexpr float kPi = 3.1415927f;

}

TEST_CASE("health and stamina are fractions that flag low at the threshold", "[combat_hud]")
{
    CombatHudInput input = base();
    CombatHudView view = buildCombatHudView(input);
    REQUIRE(view.health.fraction == Approx(0.5f));
    REQUIRE_FALSE(view.health.low);
    REQUIRE(view.stamina.fraction == Approx(0.8f));

    input.health = 25.0f;
    input.stamina = 10.0f;
    view = buildCombatHudView(input);
    REQUIRE(view.health.low);
    REQUIRE(view.stamina.low);

    input.health = 500.0f;
    REQUIRE(buildCombatHudView(input).health.fraction == 1.0f);
}

TEST_CASE("hostile bar values read as empty", "[combat_hud]")
{
    CombatHudInput input = base();
    for (float bad : {kNaN, kInf, -kInf, -5.0f}) {
        input.health = bad;
        REQUIRE(buildCombatHudView(input).health.fraction == 0.0f);
    }
    input = base();
    for (float bad : {0.0f, -1.0f, kNaN, kInf}) {
        input.maxStamina = bad;
        REQUIRE(buildCombatHudView(input).stamina.fraction == 0.0f);
    }
}

TEST_CASE("the Law resource is passed through only when there is one", "[combat_hud]")
{
    CombatHudInput input = base();
    REQUIRE_FALSE(buildCombatHudView(input).lawResource);
    input.lawResource = CombatHudLawResource{"favour_count", 2.0f, 8.0f};
    const CombatHudView view = buildCombatHudView(input);
    REQUIRE(view.lawResource);
    REQUIRE(view.lawResource->id == "favour_count");
    REQUIRE(view.lawResource->fraction == Approx(0.25f));
    REQUIRE(view.lawResource->value == 2.0f);

    input.lawResource->value = kNaN;
    REQUIRE(buildCombatHudView(input).lawResource->value == 0.0f);
}

TEST_CASE("the stagger tick exists only with a target and tracks its stagger", "[combat_hud]")
{
    CombatHudInput input = base();
    REQUIRE_FALSE(buildCombatHudView(input).targetStagger);
    input.target = CombatHudTarget{30.0f, 120.0f};
    REQUIRE(buildCombatHudView(input).targetStagger.value() == Approx(0.25f));
    input.target->stagger = 90.0f;
    REQUIRE(buildCombatHudView(input).targetStagger.value() == Approx(0.75f));
    input.target->staggerMax = 0.0f;
    REQUIRE(buildCombatHudView(input).targetStagger.value() == 0.0f);
}

TEST_CASE("damage indicators point at the attacker relative to facing", "[combat_hud]")
{
    CombatHudInput input = base();
    input.facingYaw = 1.0f;
    input.recentHits = {{1.0f, 0.0f}, {1.0f + kHalfPi, 0.0f}, {1.0f - kHalfPi, 0.0f}, {1.0f + kPi, 0.0f}};
    const CombatHudView view = buildCombatHudView(input);
    REQUIRE(view.indicators.size() == 4);
    REQUIRE(view.indicators[0].angle == Approx(0.0f).margin(1e-4));
    REQUIRE(view.indicators[1].angle == Approx(kHalfPi).margin(1e-4));
    REQUIRE(view.indicators[2].angle == Approx(-kHalfPi).margin(1e-4));
    REQUIRE(std::fabs(view.indicators[3].angle) == Approx(kPi).margin(1e-4));
}

TEST_CASE("indicator angles wrap across a full turn", "[combat_hud]")
{
    CombatHudInput input = base();
    input.facingYaw = 3.0f;
    input.recentHits = {{-3.0f, 0.0f}};
    const CombatHudView view = buildCombatHudView(input);
    REQUIRE(view.indicators[0].angle == Approx(-6.0f + 2.0f * kPi).margin(1e-4));
}

TEST_CASE("indicators fade, expire, sort newest first and are capped", "[combat_hud]")
{
    CombatHudInput input = base();
    input.recentHits = {{0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.5f}, {0.0f, 2.0f}, {0.0f, 0.75f}};
    const CombatHudView view = buildCombatHudView(input);
    REQUIRE(view.indicators.size() == 3);
    REQUIRE(view.indicators[0].opacity == Approx(1.0f));
    REQUIRE(view.indicators[1].opacity == Approx(0.5f));
    REQUIRE(view.indicators[2].opacity == Approx(1.0f / 3.0f));

    input.recentHits.assign(20, CombatHudHit{0.0f, 0.1f});
    REQUIRE(buildCombatHudView(input).indicators.size() == 8);
    CombatHudTuning tuning;
    tuning.maxIndicators = 2;
    REQUIRE(buildCombatHudView(input, tuning).indicators.size() == 2);
}

TEST_CASE("hostile hits and tuning never produce an indicator or crash", "[combat_hud]")
{
    CombatHudInput input = base();
    input.recentHits = {{kNaN, 0.0f}, {0.0f, kNaN}, {kInf, 0.0f}, {0.0f, -1.0f}, {0.0f, kInf}};
    REQUIRE(buildCombatHudView(input).indicators.empty());

    input.recentHits = {{0.0f, 0.1f}};
    input.facingYaw = kNaN;
    REQUIRE(buildCombatHudView(input).indicators.empty());
    input.facingYaw = 0.0f;
    CombatHudTuning tuning;
    tuning.indicatorSeconds = 0.0f;
    REQUIRE(buildCombatHudView(input, tuning).indicators.empty());
    tuning.indicatorSeconds = kNaN;
    REQUIRE(buildCombatHudView(input, tuning).indicators.empty());
}

TEST_CASE("an empty input yields an empty HUD", "[combat_hud]")
{
    const CombatHudView view = buildCombatHudView(CombatHudInput{});
    REQUIRE(view.health.fraction == 0.0f);
    REQUIRE(view.indicators.empty());
    REQUIRE_FALSE(view.lawResource);
    REQUIRE_FALSE(view.targetStagger);
}
