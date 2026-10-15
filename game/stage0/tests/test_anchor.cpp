#include "ichor/stage0/anchor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;
using namespace ichor::stage0;

namespace {

const AnchorTuning kTuning{};
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

bool same(const AnchorBrain& a, const AnchorBrain& b)
{
    return a.state == b.state && a.stateTicks == b.stateTicks && a.cooldownTicks == b.cooldownTicks && a.slams == b.slams;
}

bool same(const AnchorIntent& a, const AnchorIntent& b)
{
    return a.move == b.move && a.slam == b.slam && a.reaches == b.reaches && a.telegraph == b.telegraph;
}

AnchorSenses senses(float selfX, float targetX)
{
    AnchorSenses s;
    s.self = {selfX, 0.0f};
    s.target = {targetX, 0.0f};
    return s;
}

int stepsUntil(AnchorBrain& brain, AnchorSenses s, AnchorState state, int limit)
{
    for (int i = 1; i <= limit; ++i) {
        anchorStep(brain, s, kTuning);
        if (brain.anchorState() == state) return i;
    }
    return -1;
}

AnchorBrain windingUp()
{
    AnchorBrain brain;
    const AnchorSenses s = senses(0.0f, kTuning.slamRange - 0.5f);
    REQUIRE(stepsUntil(brain, s, AnchorState::Windup, 10) > 0);
    return brain;
}

}

TEST_CASE("an anchor with no target in range stays unaware and does not move")
{
    AnchorBrain brain;
    const AnchorIntent intent = anchorStep(brain, senses(0.0f, kTuning.aggroRange + 5.0f), kTuning);
    REQUIRE(brain.anchorState() == AnchorState::Unaware);
    REQUIRE(intent.move == glm::vec2(0.0f));
}

TEST_CASE("it advances slowly: slower than the skirmisher walk")
{
    AnchorBrain brain;
    const AnchorSenses s = senses(0.0f, kTuning.aggroRange - 1.0f);
    anchorStep(brain, s, kTuning);
    const AnchorIntent intent = anchorStep(brain, s, kTuning);
    REQUIRE(brain.anchorState() == AnchorState::Advance);
    REQUIRE(intent.move.x > 0.0f);
    REQUIRE(glm::length(intent.move) == kTuning.walkSpeed);
    REQUIRE(kTuning.walkSpeed < 3.5f);
}

TEST_CASE("the wind-up is a long fixed telegraph, announced once, with no movement")
{
    AnchorBrain brain;
    const AnchorSenses s = senses(0.0f, kTuning.slamRange - 0.5f);
    int cues = 0;
    int windupTicks = 0;
    for (int i = 0; i < 400 && brain.anchorState() != AnchorState::Recover; ++i) {
        const AnchorIntent intent = anchorStep(brain, s, kTuning);
        if (intent.telegraph) ++cues;
        if (brain.anchorState() == AnchorState::Windup) {
            ++windupTicks;
            REQUIRE(intent.move == glm::vec2(0.0f));
        }
    }
    REQUIRE(cues == 1);
    REQUIRE(windupTicks >= kTuning.windupTicks - 1);
    REQUIRE(kTuning.windupTicks >= 40);
}

TEST_CASE("the slam lands exactly once and only reaches a target inside the slam radius")
{
    AnchorBrain brain = windingUp();
    AnchorSenses s = senses(0.0f, kTuning.slamRadius - 0.1f);
    int slams = 0;
    bool reached = false;
    for (int i = 0; i < kTuning.windupTicks + 5; ++i) {
        const AnchorIntent intent = anchorStep(brain, s, kTuning);
        if (intent.slam) {
            ++slams;
            reached = intent.reaches;
        }
    }
    REQUIRE(slams == 1);
    REQUIRE(reached);

    AnchorBrain other = windingUp();
    s = senses(0.0f, kTuning.slamRadius + 1.0f);
    bool anyReach = false;
    int otherSlams = 0;
    for (int i = 0; i < kTuning.windupTicks + 5; ++i) {
        const AnchorIntent intent = anchorStep(other, s, kTuning);
        if (intent.slam) ++otherSlams;
        anyReach = anyReach || intent.reaches;
    }
    REQUIRE(otherSlams == 1);
    REQUIRE_FALSE(anyReach);
}

TEST_CASE("the recovery after a slam is the punish window and lasts its full length")
{
    AnchorBrain brain = windingUp();
    const AnchorSenses s = senses(0.0f, kTuning.slamRange - 0.5f);
    REQUIRE(stepsUntil(brain, s, AnchorState::Recover, 400) > 0);
    int recover = 0;
    while (brain.anchorState() == AnchorState::Recover && recover < 1000) {
        const AnchorIntent intent = anchorStep(brain, s, kTuning);
        REQUIRE(intent.move == glm::vec2(0.0f));
        REQUIRE_FALSE(intent.slam);
        ++recover;
    }
    REQUIRE(recover == kTuning.recoveryTicks);
    REQUIRE(brain.slams == 1);
}

TEST_CASE("after recovering it holds its ground until the cooldown ends instead of chasing")
{
    AnchorBrain brain = windingUp();
    const AnchorSenses s = senses(0.0f, kTuning.slamRange - 0.5f);
    REQUIRE(stepsUntil(brain, s, AnchorState::Recover, 400) > 0);
    REQUIRE(stepsUntil(brain, s, AnchorState::Hold, 400) > 0);
    int held = 0;
    while (brain.anchorState() == AnchorState::Hold && held < 1000) {
        const AnchorIntent intent = anchorStep(brain, s, kTuning);
        REQUIRE(intent.move == glm::vec2(0.0f));
        ++held;
    }
    REQUIRE(brain.anchorState() == AnchorState::Windup);
    REQUIRE(held <= kTuning.cooldownTicks + 1);
}

TEST_CASE("a stagger interrupts the wind-up: no slam, and the anchor stays exposed while staggered")
{
    AnchorBrain brain = windingUp();
    AnchorSenses s = senses(0.0f, kTuning.slamRadius - 0.1f);
    for (int i = 0; i < 10; ++i) anchorStep(brain, s, kTuning);
    s.staggered = true;
    const AnchorIntent intent = anchorStep(brain, s, kTuning);
    REQUIRE(brain.anchorState() == AnchorState::Staggered);
    REQUIRE_FALSE(intent.slam);
    for (int i = 0; i < 200; ++i) {
        const AnchorIntent held = anchorStep(brain, s, kTuning);
        REQUIRE_FALSE(held.slam);
        REQUIRE(held.move == glm::vec2(0.0f));
    }
    REQUIRE(brain.slams == 0);
    s.staggered = false;
    anchorStep(brain, s, kTuning);
    REQUIRE(brain.anchorState() != AnchorState::Staggered);
    REQUIRE(brain.cooldownTicks > 0);
}

TEST_CASE("the anchor's poise is more than twice the standard fighter's, so a light hit does not stagger it")
{
    REQUIRE(kTuning.maxPoise > 100.0f * 2.0f);
}

TEST_CASE("a dead target ends the aggression, even mid wind-up")
{
    AnchorBrain brain = windingUp();
    AnchorSenses s = senses(0.0f, 1.0f);
    s.targetAlive = false;
    const AnchorIntent intent = anchorStep(brain, s, kTuning);
    REQUIRE(brain.anchorState() == AnchorState::Unaware);
    REQUIRE_FALSE(intent.slam);
    REQUIRE(intent.move == glm::vec2(0.0f));
}

TEST_CASE("non-finite positions are rejected before any cast and leave the brain untouched")
{
    for (const float bad : {kNaN, kInf, -kInf}) {
        AnchorBrain brain = windingUp();
        const AnchorBrain before = brain;
        AnchorSenses s = senses(0.0f, 1.0f);
        s.target.x = bad;
        AnchorIntent intent = anchorStep(brain, s, kTuning);
        REQUIRE(same(brain, before));
        REQUIRE(intent.move == glm::vec2(0.0f));
        REQUIRE_FALSE(intent.slam);

        s = senses(0.0f, 1.0f);
        s.self.y = bad;
        intent = anchorStep(brain, s, kTuning);
        REQUIRE(same(brain, before));
        REQUIRE_FALSE(intent.slam);
    }
}

TEST_CASE("a target at zero distance and an astronomically far one are both safe")
{
    AnchorBrain brain;
    for (int i = 0; i < 200; ++i) {
        const AnchorIntent intent = anchorStep(brain, senses(3.0f, 3.0f), kTuning);
        REQUIRE(std::isfinite(intent.move.x));
        REQUIRE(std::isfinite(intent.move.y));
    }

    AnchorBrain far;
    const AnchorIntent intent = anchorStep(far, senses(0.0f, 3.0e38f), kTuning);
    REQUIRE(far.anchorState() == AnchorState::Unaware);
    REQUIRE(intent.move == glm::vec2(0.0f));
}

TEST_CASE("loaded brain state is clamped: a runaway counter or unknown state is pulled back in")
{
    AnchorBrain brain;
    brain.state = 250;
    brain.stateTicks = -5;
    brain.cooldownTicks = 1 << 30;
    anchorStep(brain, senses(0.0f, 1.0f), kTuning);
    REQUIRE(brain.state <= static_cast<std::uint8_t>(AnchorState::Staggered));
    REQUIRE(brain.stateTicks >= 0);
    REQUIRE(brain.cooldownTicks <= kTuning.cooldownTicks);
}

TEST_CASE("the same inputs give the same brain, bit for bit")
{
    AnchorBrain a;
    AnchorBrain b;
    for (int i = 0; i < 1000; ++i) {
        AnchorSenses s = senses(0.0f, 8.0f - 0.01f * static_cast<float>(i % 700));
        s.staggered = i % 331 == 330;
        const AnchorIntent x = anchorStep(a, s, kTuning);
        const AnchorIntent y = anchorStep(b, s, kTuning);
        REQUIRE(same(x, y));
    }
    REQUIRE(same(a, b));
    REQUIRE(a.slams > 0);
}
