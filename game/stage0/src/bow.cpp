#include "ichor/stage0/bow.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace ichor::stage0 {

namespace {

bool inRange(std::int32_t v)
{
    return v >= -kMaxBowCoordMm && v <= kMaxBowCoordMm;
}

std::int64_t isqrt(std::int64_t n)
{
    std::int64_t r = 0;
    for (std::int64_t bit = std::int64_t{1} << 30; bit != 0; bit >>= 1) {
        if ((r + bit) * (r + bit) <= n) {
            r += bit;
        }
    }
    return r;
}

bool validTuning(const BowTuning& t)
{
    return t.projectileSpeedMm >= 1 && t.projectileSpeedMm <= kMaxProjectileSpeedMm && t.baseReachMm >= 0 && t.baseReachMm <= 100000
        && t.minDrawPermille >= 0 && t.minDrawPermille <= 1000 && t.targetRadiusMm >= 0 && t.targetRadiusMm <= 100000 && t.baseDamage >= 0
        && t.baseDamage <= 100000 && t.openerBonusPermille >= 0 && t.openerBonusPermille <= 100000;
}

}

const char* name(BowState state)
{
    switch (state) {
    case BowState::Idle: return "idle";
    case BowState::Drawing: return "drawing";
    case BowState::Holding: return "holding";
    case BowState::Releasing: return "releasing";
    case BowState::Recovering: return "recovering";
    }
    return "unknown";
}

std::int32_t drawPermille(std::int32_t drawnTicks, const WeaponClass& cls, const BowTuning& tuning)
{
    if (drawnTicks <= 0 || cls.windupTicks <= 0) {
        return 0;
    }
    if (drawnTicks >= cls.windupTicks) {
        return 1000;
    }
    const std::int64_t span = std::max<std::int64_t>(std::int64_t{cls.windupTicks} - tuning.minDrawTicks, 1);
    const std::int64_t above = std::max<std::int64_t>(std::int64_t{drawnTicks} - tuning.minDrawTicks, 0);
    const std::int64_t power = tuning.minDrawPermille + (1000 - tuning.minDrawPermille) * above / span;
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(power, 0, 1000));
}

std::int32_t bowRangeMm(const WeaponClass& cls, const BowTuning& tuning)
{
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(std::int64_t{cls.reachPermille} * tuning.baseReachMm / 1000, 0, kMaxBowCoordMm));
}

BowEvent bowStep(BowBrain& brain, bool drawHeld, const WeaponClass& cls, const BowTuning& tuning)
{
    BowEvent event;
    if (!cls.ranged || cls.windupTicks <= 0 || cls.strikeTicks <= 0 || cls.recoveryTicks <= 0 || cls.staminaCost < 0) {
        brain = BowBrain{};
        return event;
    }
    if (brain.stateTicks < 0 || brain.stateTicks > kMaxBowStateTicks) {
        brain = BowBrain{};
    }
    const auto enter = [&](BowState next) {
        brain.state = static_cast<std::uint8_t>(next);
        brain.stateTicks = 0;
    };
    switch (brain.bowState()) {
    case BowState::Idle:
        if (drawHeld) {
            enter(BowState::Drawing);
        }
        break;
    case BowState::Drawing:
    case BowState::Holding: {
        ++brain.stateTicks;
        const bool holding = brain.bowState() == BowState::Holding;
        const std::int32_t drawn = holding ? cls.windupTicks : brain.stateTicks;
        if (!drawHeld) {
            if (!holding && drawn < tuning.minDrawTicks) {
                event.cancelled = true;
                enter(BowState::Idle);
            } else {
                event.fired = true;
                event.drawPermille = drawPermille(drawn, cls, tuning);
                event.staminaSpent = cls.staminaCost;
                enter(BowState::Releasing);
            }
        } else if (!holding && brain.stateTicks >= cls.windupTicks) {
            enter(BowState::Holding);
        } else if (holding && brain.stateTicks >= tuning.maxHoldTicks) {
            event.cancelled = true;
            enter(BowState::Recovering);
        }
        break;
    }
    case BowState::Releasing:
        if (++brain.stateTicks >= cls.strikeTicks) {
            enter(BowState::Recovering);
        }
        break;
    case BowState::Recovering:
        if (++brain.stateTicks >= cls.recoveryTicks) {
            enter(BowState::Idle);
        }
        break;
    default:
        enter(BowState::Idle);
        break;
    }
    return event;
}

std::optional<Projectile> launchProjectile(std::int32_t x, std::int32_t y, std::int32_t dirX, std::int32_t dirY, std::int32_t drawPermille,
    const WeaponClass& cls, const BowTuning& tuning)
{
    if (!inRange(x) || !inRange(y) || !inRange(dirX) || !inRange(dirY) || (dirX == 0 && dirY == 0) || drawPermille < 0 || drawPermille > 1000
        || !validTuning(tuning)) {
        return std::nullopt;
    }
    // Scaled up before the square root so a short aim vector keeps its direction and the speed stays true.
    const std::int64_t sx = std::int64_t{dirX} * 1024;
    const std::int64_t sy = std::int64_t{dirY} * 1024;
    const std::int64_t length = isqrt(sx * sx + sy * sy);
    Projectile arrow;
    arrow.x = x;
    arrow.y = y;
    arrow.vx = static_cast<std::int32_t>(sx * tuning.projectileSpeedMm / length);
    arrow.vy = static_cast<std::int32_t>(sy * tuning.projectileSpeedMm / length);
    arrow.remainingMm = bowRangeMm(cls, tuning);
    arrow.drawPermille = drawPermille;
    arrow.alive = arrow.remainingMm > 0 && (arrow.vx != 0 || arrow.vy != 0);
    return arrow;
}

ProjectileResult projectileStep(Projectile& arrow, std::int32_t targetX, std::int32_t targetY, const BowTuning& tuning)
{
    ProjectileResult result;
    if (!arrow.alive) {
        return result;
    }
    if (!inRange(arrow.x) || !inRange(arrow.y) || std::abs(std::int64_t{arrow.vx}) > 2 * kMaxProjectileSpeedMm
        || std::abs(std::int64_t{arrow.vy}) > 2 * kMaxProjectileSpeedMm || arrow.remainingMm <= 0 || !validTuning(tuning)) {
        arrow.alive = false;
        return result;
    }
    const std::int64_t vv = std::int64_t{arrow.vx} * arrow.vx + std::int64_t{arrow.vy} * arrow.vy;
    if (vv == 0 || vv > std::int64_t{kMaxProjectileSpeedMm} * kMaxProjectileSpeedMm * 2) {
        arrow.alive = false;
        return result;
    }
    // The last step is shortened so the arrow never flies past its range.
    const std::int64_t speed = isqrt(vv);
    const std::int64_t stepMm = std::min<std::int64_t>(speed, arrow.remainingMm);
    const std::int64_t sx = arrow.vx * stepMm / speed;
    const std::int64_t sy = arrow.vy * stepMm / speed;
    const std::int64_t ss = sx * sx + sy * sy;

    if (inRange(targetX) && inRange(targetY) && ss > 0) {
        const std::int64_t wx = std::int64_t{targetX} - arrow.x;
        const std::int64_t wy = std::int64_t{targetY} - arrow.y;
        const std::int64_t along = std::clamp<std::int64_t>(wx * sx + wy * sy, 0, ss);
        const std::int64_t dx = wx - sx * along / ss;
        const std::int64_t dy = wy - sy * along / ss;
        const std::int64_t r = tuning.targetRadiusMm;
        if (dx * dx + dy * dy <= r * r) {
            result.hit = true;
        }
    }
    arrow.x += static_cast<std::int32_t>(sx);
    arrow.y += static_cast<std::int32_t>(sy);
    arrow.remainingMm -= static_cast<std::int32_t>(stepMm);
    if (result.hit || arrow.remainingMm <= 0) {
        arrow.alive = false;
    }
    return result;
}

std::int32_t arrowDamage(std::int32_t drawPermille, bool opener, const BowTuning& tuning)
{
    if (drawPermille < 0 || drawPermille > 1000 || !validTuning(tuning)) {
        return 0;
    }
    std::int64_t damage = std::int64_t{tuning.baseDamage} * drawPermille / 1000;
    if (opener) {
        damage = damage * tuning.openerBonusPermille / 1000;
    }
    return static_cast<std::int32_t>(std::min<std::int64_t>(damage, INT32_MAX));
}

void openingNoteShot(RangedOpening& opening)
{
    if (opening.shots < INT32_MAX) {
        ++opening.shots;
    }
}

std::int32_t openingNoteHit(RangedOpening& opening, std::int32_t drawPermille, bool targetUnaware, const BowTuning& tuning)
{
    if (drawPermille < 0 || drawPermille > 1000 || !validTuning(tuning)) {
        return 0;
    }
    const bool opener = targetUnaware && !opening.openerSpent;
    opening.openerSpent = true;
    if (opening.hits < INT32_MAX) {
        ++opening.hits;
    }
    if (opening.openingPhase() == OpeningPhase::Approach) {
        opening.phase = static_cast<std::uint8_t>(OpeningPhase::Opened);
    }
    return arrowDamage(drawPermille, opener, tuning);
}

void openingStep(RangedOpening& opening, std::int32_t distanceMm, const BowTuning& tuning)
{
    if (distanceMm <= tuning.meleeRangeMm) {
        opening.phase = static_cast<std::uint8_t>(OpeningPhase::Closed);
    }
}

}
