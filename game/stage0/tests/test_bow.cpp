#include "ichor/stage0/bow.h"

#include <catch2/catch_test_macros.hpp>

#include <climits>

using namespace ichor::stage0;

namespace {

WeaponClass bowClass()
{
    WeaponClass c;
    c.id = "bow";
    c.windupTicks = 40;
    c.strikeTicks = 4;
    c.recoveryTicks = 18;
    c.staminaCost = 16;
    c.poiseDamage = 18;
    c.reachPermille = 12000;
    c.ranged = true;
    return c;
}

const BowTuning kTuning{};

BowEvent run(BowBrain& brain, bool held, int ticks, const WeaponClass& cls)
{
    BowEvent last;
    for (int i = 0; i < ticks; ++i) {
        const BowEvent e = bowStep(brain, held, cls, kTuning);
        if (e.fired || e.cancelled) {
            last = e;
        }
    }
    return last;
}

}

TEST_CASE("a full draw takes the class windup and fires at full power", "[bow]")
{
    const WeaponClass cls = bowClass();
    BowBrain brain;
    run(brain, true, 1 + cls.windupTicks, cls);
    CHECK(brain.bowState() == BowState::Holding);
    const BowEvent e = bowStep(brain, false, cls, kTuning);
    CHECK(e.fired);
    CHECK(e.drawPermille == 1000);
    CHECK(e.staminaSpent == cls.staminaCost);
}

TEST_CASE("an early release is weaker than a full draw", "[bow]")
{
    const WeaponClass cls = bowClass();
    BowBrain brain;
    run(brain, true, 1 + 20, cls);
    const BowEvent e = bowStep(brain, false, cls, kTuning);
    REQUIRE(e.fired);
    CHECK(e.drawPermille < 1000);
    CHECK(e.drawPermille > kTuning.minDrawPermille);
    CHECK(arrowDamage(e.drawPermille, false, kTuning) < arrowDamage(1000, false, kTuning));
}

TEST_CASE("a release before the minimum draw cancels for free", "[bow]")
{
    const WeaponClass cls = bowClass();
    BowBrain brain;
    run(brain, true, 1 + 3, cls);
    const BowEvent e = bowStep(brain, false, cls, kTuning);
    CHECK_FALSE(e.fired);
    CHECK(e.cancelled);
    CHECK(e.staminaSpent == 0);
    CHECK(brain.bowState() == BowState::Idle);
}

TEST_CASE("holding too long loses the arrow and costs recovery", "[bow]")
{
    const WeaponClass cls = bowClass();
    BowBrain brain;
    const BowEvent e = run(brain, true, 1 + cls.windupTicks + kTuning.maxHoldTicks, cls);
    CHECK(e.cancelled);
    CHECK_FALSE(e.fired);
    CHECK(brain.bowState() == BowState::Recovering);
}

TEST_CASE("the nock commitment cannot be skipped: no second shot until recovery ends", "[bow]")
{
    const WeaponClass cls = bowClass();
    BowBrain brain;
    run(brain, true, 1 + cls.windupTicks, cls);
    REQUIRE(bowStep(brain, false, cls, kTuning).fired);
    int fired = 0;
    for (int i = 0; i < cls.strikeTicks + cls.recoveryTicks - 1; ++i) {
        fired += bowStep(brain, true, cls, kTuning).fired ? 1 : 0;
        CHECK(brain.bowState() != BowState::Drawing);
    }
    CHECK(fired == 0);
    bowStep(brain, true, cls, kTuning);
    CHECK(brain.bowState() == BowState::Idle);
}

TEST_CASE("a corrupt state byte recovers to idle", "[bow]")
{
    BowBrain brain;
    brain.state = 200;
    bowStep(brain, true, bowClass(), kTuning);
    CHECK(brain.bowState() == BowState::Idle);
}

TEST_CASE("an arrow hits a target on its line and misses one off it", "[bow][projectile]")
{
    const WeaponClass cls = bowClass();
    auto arrow = launchProjectile(0, 0, 1, 0, 1000, cls, kTuning);
    REQUIRE(arrow);
    bool hit = false;
    int steps = 0;
    while (arrow->alive && steps++ < 100) {
        hit = projectileStep(*arrow, 6000, 0, kTuning).hit;
    }
    CHECK(hit);
    CHECK_FALSE(arrow->alive);

    auto wide = launchProjectile(0, 0, 1, 0, 1000, cls, kTuning);
    REQUIRE(wide);
    bool wideHit = false;
    while (wide->alive) {
        wideHit = wideHit || projectileStep(*wide, 6000, kTuning.targetRadiusMm + 50, kTuning).hit;
    }
    CHECK_FALSE(wideHit);
}

TEST_CASE("a fast arrow does not tunnel through a thin target", "[bow][projectile]")
{
    BowTuning fast = kTuning;
    fast.projectileSpeedMm = 5000;
    fast.targetRadiusMm = 100;
    auto arrow = launchProjectile(0, 0, 0, 7, 1000, bowClass(), fast);
    REQUIRE(arrow);
    CHECK_FALSE(projectileStep(*arrow, 0, 7500, fast).hit);
    auto second = launchProjectile(0, 0, 0, 7, 1000, bowClass(), fast);
    REQUIRE(second);
    CHECK(projectileStep(*second, 0, 2500, fast).hit);
}

TEST_CASE("the arrow stops at its range", "[bow][projectile]")
{
    const WeaponClass cls = bowClass();
    auto arrow = launchProjectile(0, 0, -3, -4, 1000, cls, kTuning);
    REQUIRE(arrow);
    int steps = 0;
    while (arrow->alive && steps < 10000) {
        projectileStep(*arrow, 500000, 500000, kTuning);
        ++steps;
    }
    CHECK_FALSE(arrow->alive);
    const std::int64_t range = bowRangeMm(cls, kTuning);
    CHECK(steps == (range + kTuning.projectileSpeedMm - 1) / kTuning.projectileSpeedMm);
    CHECK(std::int64_t{arrow->x} * arrow->x + std::int64_t{arrow->y} * arrow->y <= range * range);

    auto beyond = launchProjectile(0, 0, 1, 0, 1000, cls, kTuning);
    REQUIRE(beyond);
    bool hit = false;
    while (beyond->alive) {
        hit = hit || projectileStep(*beyond, bowRangeMm(cls, kTuning) + 2000, 0, kTuning).hit;
    }
    CHECK_FALSE(hit);
}

TEST_CASE("hostile launch and step inputs are rejected without overflow", "[bow][projectile]")
{
    const WeaponClass cls = bowClass();
    CHECK_FALSE(launchProjectile(0, 0, 0, 0, 1000, cls, kTuning));
    CHECK_FALSE(launchProjectile(INT_MAX, 0, 1, 0, 1000, cls, kTuning));
    CHECK_FALSE(launchProjectile(0, 0, INT_MIN, 1, 1000, cls, kTuning));
    CHECK_FALSE(launchProjectile(0, 0, 1, 0, 1001, cls, kTuning));
    BowTuning bad = kTuning;
    bad.projectileSpeedMm = INT_MAX;
    CHECK_FALSE(launchProjectile(0, 0, 1, 0, 1000, cls, bad));
    auto arrow = launchProjectile(0, 0, 1, 0, 1000, cls, kTuning);
    REQUIRE(arrow);
    CHECK_FALSE(projectileStep(*arrow, INT_MAX, INT_MIN, kTuning).hit);
    arrow->vx = INT_MAX;
    CHECK_FALSE(projectileStep(*arrow, 0, 0, kTuning).hit);
    CHECK_FALSE(arrow->alive);
}

TEST_CASE("the opener bonus applies once, to an unaware target only", "[bow][opening]")
{
    RangedOpening opening;
    const auto plain = arrowDamage(1000, false, kTuning);
    CHECK(openingNoteHit(opening, 1000, false, kTuning) == plain);
    CHECK(opening.openingPhase() == OpeningPhase::Opened);
    CHECK(openingNoteHit(opening, 1000, true, kTuning) == plain);

    RangedOpening fresh;
    const auto opener = openingNoteHit(fresh, 1000, true, kTuning);
    CHECK(opener > plain);
    CHECK(opener == plain * kTuning.openerBonusPermille / 1000);
    CHECK(openingNoteHit(fresh, 1000, true, kTuning) == plain);
    CHECK(fresh.hits == 2);
}

TEST_CASE("closing to melee ends the opening and it does not reopen", "[bow][opening]")
{
    RangedOpening opening;
    openingStep(opening, 10000, kTuning);
    CHECK(opening.openingPhase() == OpeningPhase::Approach);
    openingNoteHit(opening, 1000, true, kTuning);
    openingStep(opening, kTuning.meleeRangeMm + 1, kTuning);
    CHECK(opening.openingPhase() == OpeningPhase::Opened);
    openingStep(opening, kTuning.meleeRangeMm, kTuning);
    CHECK(opening.openingPhase() == OpeningPhase::Closed);
    openingStep(opening, 20000, kTuning);
    CHECK(opening.openingPhase() == OpeningPhase::Closed);
    openingNoteHit(opening, 1000, false, kTuning);
    CHECK(opening.openingPhase() == OpeningPhase::Closed);
}

TEST_CASE("a two-shot opening on an unaware target outdamages two plain full draws", "[bow][opening]")
{
    const WeaponClass cls = bowClass();
    BowBrain brain;
    RangedOpening opening;
    int damage = 0;
    for (int tick = 0; tick < 300 && opening.shots < 2; ++tick) {
        const bool held = (tick % 70) < 45;
        const BowEvent e = bowStep(brain, held, cls, kTuning);
        if (!e.fired) {
            continue;
        }
        openingNoteShot(opening);
        auto arrow = launchProjectile(0, 0, 1, 0, e.drawPermille, cls, kTuning);
        REQUIRE(arrow);
        while (arrow->alive) {
            if (projectileStep(*arrow, 8000, 0, kTuning).hit) {
                damage += openingNoteHit(opening, e.drawPermille, true, kTuning);
            }
        }
    }
    CHECK(opening.shots == 2);
    CHECK(opening.hits == 2);
    CHECK(damage > 2 * arrowDamage(1000, false, kTuning));
}
