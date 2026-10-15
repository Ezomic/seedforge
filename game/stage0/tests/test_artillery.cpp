#include "ichor/stage0/artillery.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <cmath>
#include <limits>

using namespace ichor;
using namespace ichor::stage0;

namespace {

const ArtilleryTuning kTuning{};
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
const float kBand = (kTuning.minRange + kTuning.maxRange) * 0.5f;

bool same(const ArtilleryBrain& a, const ArtilleryBrain& b)
{
    return a.state == b.state && a.stateTicks == b.stateTicks && a.cooldownTicks == b.cooldownTicks && a.shots == b.shots && a.aimX == b.aimX
        && a.aimY == b.aimY;
}

bool same(const ArtilleryIntent& a, const ArtilleryIntent& b)
{
    return a.move == b.move && a.fire == b.fire && a.hits == b.hits && a.telegraph == b.telegraph && a.aimAt == b.aimAt;
}

ArtillerySenses senses(float selfX, float targetX)
{
    ArtillerySenses s;
    s.self = {selfX, 0.0f};
    s.target = {targetX, 0.0f};
    return s;
}

int stepsUntil(ArtilleryBrain& brain, ArtillerySenses s, ArtilleryState state, int limit)
{
    for (int i = 1; i <= limit; ++i) {
        artilleryStep(brain, s, kTuning);
        if (brain.artilleryState() == state) return i;
    }
    return -1;
}

ArtilleryBrain aiming()
{
    ArtilleryBrain brain;
    REQUIRE(stepsUntil(brain, senses(0.0f, kBand), ArtilleryState::Aim, 10) > 0);
    return brain;
}

}

TEST_CASE("an artillery with no target in range stays unaware and does not move")
{
    ArtilleryBrain brain;
    const ArtilleryIntent intent = artilleryStep(brain, senses(0.0f, kTuning.aggroRange + 5.0f), kTuning);
    REQUIRE(brain.artilleryState() == ArtilleryState::Unaware);
    REQUIRE(intent.move == glm::vec2(0.0f));
}

TEST_CASE("beyond its band it moves up; inside the band with a shot it stops and aims")
{
    ArtilleryBrain brain;
    const ArtillerySenses far = senses(0.0f, kTuning.maxRange + 4.0f);
    artilleryStep(brain, far, kTuning);
    const ArtilleryIntent move = artilleryStep(brain, far, kTuning);
    REQUIRE(brain.artilleryState() == ArtilleryState::Reposition);
    REQUIRE(move.move.x == kTuning.walkSpeed);
    REQUIRE_FALSE(move.telegraph);

    const ArtilleryIntent aim = artilleryStep(brain, senses(0.0f, kBand), kTuning);
    REQUIRE(brain.artilleryState() == ArtilleryState::Aim);
    REQUIRE(aim.telegraph);
    REQUIRE(aim.move == glm::vec2(0.0f));
}

TEST_CASE("without line of sight it repositions toward the target instead of firing")
{
    ArtilleryBrain brain;
    ArtillerySenses s = senses(0.0f, kBand);
    s.lineOfSight = false;
    artilleryStep(brain, s, kTuning);
    for (int i = 0; i < 100; ++i) {
        const ArtilleryIntent intent = artilleryStep(brain, s, kTuning);
        REQUIRE_FALSE(intent.telegraph);
        REQUIRE(intent.move.x > 0.0f);
    }
    REQUIRE(brain.shots == 0);
    REQUIRE(brain.artilleryState() == ArtilleryState::Reposition);

    s.lineOfSight = true;
    artilleryStep(brain, s, kTuning);
    REQUIRE(brain.artilleryState() == ArtilleryState::Aim);
}

TEST_CASE("a target that closes inside the minimum range is backed away from and never fired on")
{
    ArtilleryBrain brain;
    const ArtillerySenses close = senses(0.0f, kTuning.minRange - 2.0f);
    artilleryStep(brain, close, kTuning);
    for (int i = 0; i < 200; ++i) {
        const ArtilleryIntent intent = artilleryStep(brain, close, kTuning);
        REQUIRE(intent.move.x == -kTuning.retreatSpeed);
        REQUIRE_FALSE(intent.telegraph);
        REQUIRE_FALSE(intent.fire);
    }
    REQUIRE(brain.shots == 0);
    REQUIRE(kTuning.retreatSpeed > kTuning.walkSpeed);
}

TEST_CASE("the aim is a fixed telegraph, announced once, with no movement, at a locked point")
{
    ArtilleryBrain brain;
    ArtillerySenses s = senses(0.0f, kBand);
    int cues = 0;
    int aimTicks = 0;
    for (int i = 0; i < 400 && brain.artilleryState() != ArtilleryState::Recover; ++i) {
        const ArtilleryIntent intent = artilleryStep(brain, s, kTuning);
        if (intent.telegraph) ++cues;
        if (brain.artilleryState() == ArtilleryState::Aim) {
            ++aimTicks;
            REQUIRE(intent.move == glm::vec2(0.0f));
            REQUIRE(intent.aimAt.x == kBand);
        }
    }
    REQUIRE(cues == 1);
    REQUIRE(aimTicks == kTuning.windupTicks);
}

TEST_CASE("the blast lands at the locked point: a target that left it is missed, one that stayed is hit")
{
    ArtilleryBrain brain = aiming();
    ArtillerySenses s = senses(0.0f, kBand + kTuning.blastRadius + 1.0f);
    ArtilleryIntent last;
    int fires = 0;
    for (int i = 0; i < kTuning.windupTicks + 5; ++i) {
        const ArtilleryIntent intent = artilleryStep(brain, s, kTuning);
        if (intent.fire) {
            ++fires;
            last = intent;
        }
    }
    REQUIRE(fires == 1);
    REQUIRE_FALSE(last.hits);
    REQUIRE(last.aimAt.x == kBand);

    ArtilleryBrain still = aiming();
    s = senses(0.0f, kBand + kTuning.blastRadius - 0.1f);
    bool hit = false;
    for (int i = 0; i < kTuning.windupTicks + 5; ++i) hit = hit || artilleryStep(still, s, kTuning).hits;
    REQUIRE(hit);
}

TEST_CASE("after firing it recovers for its full length, then waits out the cooldown before the next aim")
{
    ArtilleryBrain brain = aiming();
    const ArtillerySenses s = senses(0.0f, kBand);
    REQUIRE(stepsUntil(brain, s, ArtilleryState::Recover, 400) > 0);
    int recover = 0;
    while (brain.artilleryState() == ArtilleryState::Recover && recover < 1000) {
        const ArtilleryIntent intent = artilleryStep(brain, s, kTuning);
        REQUIRE_FALSE(intent.fire);
        ++recover;
    }
    REQUIRE(recover == kTuning.recoveryTicks);
    int waited = 0;
    while (brain.artilleryState() == ArtilleryState::Reposition && waited < 1000) {
        artilleryStep(brain, s, kTuning);
        ++waited;
    }
    REQUIRE(brain.artilleryState() == ArtilleryState::Aim);
    REQUIRE(waited == kTuning.cooldownTicks);
    REQUIRE(brain.shots == 1);
}

TEST_CASE("a stagger cancels the aim: no shot, and it stays exposed while staggered")
{
    ArtilleryBrain brain = aiming();
    ArtillerySenses s = senses(0.0f, kBand);
    for (int i = 0; i < 10; ++i) artilleryStep(brain, s, kTuning);
    s.staggered = true;
    for (int i = 0; i < 200; ++i) {
        const ArtilleryIntent intent = artilleryStep(brain, s, kTuning);
        REQUIRE(brain.artilleryState() == ArtilleryState::Staggered);
        REQUIRE_FALSE(intent.fire);
        REQUIRE(intent.move == glm::vec2(0.0f));
    }
    REQUIRE(brain.shots == 0);
    s.staggered = false;
    artilleryStep(brain, s, kTuning);
    REQUIRE(brain.artilleryState() == ArtilleryState::Reposition);
    REQUIRE(brain.cooldownTicks > 0);
}

TEST_CASE("it is fragile: low health and low poise compared with the standard fighter and the anchor")
{
    REQUIRE(kTuning.maxHealth < 100.0f);
    REQUIRE(kTuning.maxPoise < 100.0f);
}

TEST_CASE("a stagger on a tick with unusable positions still cancels the aim")
{
    ArtilleryBrain brain = aiming();
    ArtillerySenses s = senses(0.0f, kNaN);
    s.staggered = true;
    const ArtilleryIntent intent = artilleryStep(brain, s, kTuning);
    REQUIRE(brain.artilleryState() == ArtilleryState::Staggered);
    REQUIRE_FALSE(intent.fire);
}

TEST_CASE("a target standing on the artillery does not move it or produce NaN")
{
    ArtilleryBrain brain;
    for (int i = 0; i < 200; ++i) {
        const ArtilleryIntent intent = artilleryStep(brain, senses(3.0f, 3.0f), kTuning);
        REQUIRE(intent.move == glm::vec2(0.0f));
    }
    REQUIRE(brain.shots == 0);
}

TEST_CASE("a dead target ends the aggression, even mid aim")
{
    ArtilleryBrain brain = aiming();
    ArtillerySenses s = senses(0.0f, kBand);
    s.targetAlive = false;
    const ArtilleryIntent intent = artilleryStep(brain, s, kTuning);
    REQUIRE(brain.artilleryState() == ArtilleryState::Unaware);
    REQUIRE_FALSE(intent.fire);
}

TEST_CASE("non-finite positions are rejected before any cast and leave the brain untouched")
{
    for (const float bad : {kNaN, kInf, -kInf}) {
        ArtilleryBrain brain = aiming();
        const ArtilleryBrain before = brain;
        ArtillerySenses s = senses(0.0f, kBand);
        s.target.x = bad;
        ArtilleryIntent intent = artilleryStep(brain, s, kTuning);
        REQUIRE(same(brain, before));
        REQUIRE(intent.move == glm::vec2(0.0f));
        REQUIRE_FALSE(intent.fire);

        s = senses(0.0f, kBand);
        s.self.y = bad;
        intent = artilleryStep(brain, s, kTuning);
        REQUIRE(same(brain, before));
        REQUIRE_FALSE(intent.fire);
    }
}

TEST_CASE("an astronomically far target is ignored")
{
    ArtilleryBrain brain;
    const ArtilleryIntent intent = artilleryStep(brain, senses(0.0f, 3.0e38f), kTuning);
    REQUIRE(brain.artilleryState() == ArtilleryState::Unaware);
    REQUIRE(intent.move == glm::vec2(0.0f));
}

TEST_CASE("loaded brain state is clamped: counters, unknown state and a non-finite aim point are pulled back in")
{
    ArtilleryBrain brain;
    brain.state = 250;
    brain.stateTicks = -5;
    brain.cooldownTicks = 1 << 30;
    brain.aimX = kNaN;
    brain.aimY = kInf;
    artilleryStep(brain, senses(0.0f, 1.0f), kTuning);
    REQUIRE(brain.state <= static_cast<std::uint8_t>(ArtilleryState::Staggered));
    REQUIRE(brain.stateTicks >= 0);
    REQUIRE(brain.cooldownTicks <= kTuning.cooldownTicks);
    REQUIRE(std::isfinite(brain.aimX));
    REQUIRE(std::isfinite(brain.aimY));

    ArtilleryBrain hostile;
    hostile.state = static_cast<std::uint8_t>(ArtilleryState::Aim);
    hostile.aimX = kNaN;
    hostile.stateTicks = kTuning.windupTicks;
    const ArtilleryIntent intent = artilleryStep(hostile, senses(0.0f, kBand), kTuning);
    REQUIRE(std::isfinite(intent.aimAt.x));
}

TEST_CASE("the same inputs give the same brain, bit for bit")
{
    ArtilleryBrain a;
    ArtilleryBrain b;
    for (int i = 0; i < 1500; ++i) {
        ArtillerySenses s = senses(0.0f, 20.0f - 0.02f * static_cast<float>(i % 700));
        s.staggered = i % 331 == 330;
        s.lineOfSight = i % 97 != 0;
        const ArtilleryIntent x = artilleryStep(a, s, kTuning);
        const ArtilleryIntent y = artilleryStep(b, s, kTuning);
        REQUIRE(same(x, y));
    }
    REQUIRE(same(a, b));
    REQUIRE(a.shots > 0);
}
