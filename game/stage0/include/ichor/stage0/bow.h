#pragma once

#include "ichor/stage0/weapon_class.h"

#include <cstdint>
#include <optional>

namespace ichor::stage0 {

// The bow: draw and nock timing, integer projectile flight and the ranged opening that hands over to melee.
// Positions are millimetres, times are whole ticks, fractions are permille. No floats, so replays match everywhere.
constexpr std::int32_t kMaxBowCoordMm = 1000000;
constexpr std::int32_t kMaxProjectileSpeedMm = 10000;
constexpr std::int32_t kMaxBowStateTicks = 1000000;

struct BowTuning {
    std::int32_t baseReachMm = 1000;         // the class's reach_permille is a multiple of this
    std::int32_t minDrawTicks = 10;          // a release before this is a cancel: no arrow, no stamina spent
    std::int32_t maxHoldTicks = 90;          // at full draw the arms give out after this long and the arrow is lost
    std::int32_t projectileSpeedMm = 500;    // per tick
    std::int32_t minDrawPermille = 300;      // power of the earliest legal release
    std::int32_t baseDamage = 20;
    std::int32_t openerBonusPermille = 1500; // the first hit on an unaware target
    std::int32_t targetRadiusMm = 400;
    std::int32_t meleeRangeMm = 2500;        // inside this the opening is over
};

enum class BowState : std::uint8_t { Idle, Drawing, Holding, Releasing, Recovering };
const char* name(BowState state);

struct BowBrain {
    std::uint8_t state = 0;
    std::int32_t stateTicks = 0;

    BowState bowState() const { return static_cast<BowState>(state); }
};

struct BowEvent {
    bool fired = false;     // the arrow leaves the string this tick
    bool cancelled = false; // the draw was dropped early, or the arms gave out
    std::int32_t drawPermille = 0;
    std::int32_t staminaSpent = 0;
};

// One fixed tick. drawHeld is the attack button. Drawing takes the class windup, Releasing its strike, Recovering its
// recovery; nothing after the release can be cancelled, so there is no second shot until the nock is done. A held button
// starts the next draw as soon as the bow is idle. A class that is not ranged or has a non-positive time never fires.
BowEvent bowStep(BowBrain& brain, bool drawHeld, const WeaponClass& cls, const BowTuning& tuning);

// Draw power in permille: minDrawPermille at minDrawTicks rising linearly to 1000 at full draw.
std::int32_t drawPermille(std::int32_t drawnTicks, const WeaponClass& cls, const BowTuning& tuning);

// Maximum flight distance of an arrow from this class.
std::int32_t bowRangeMm(const WeaponClass& cls, const BowTuning& tuning);

struct Projectile {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t vx = 0; // per tick
    std::int32_t vy = 0;
    std::int32_t remainingMm = 0;
    std::int32_t drawPermille = 0;
    bool alive = false;
};

// Aim at (dirX, dirY), any nonzero integer vector. Nullopt for a zero or out-of-range aim, origin, draw or tuning.
std::optional<Projectile> launchProjectile(std::int32_t x, std::int32_t y, std::int32_t dirX, std::int32_t dirY, std::int32_t drawPermille,
    const WeaponClass& cls, const BowTuning& tuning);

struct ProjectileResult {
    bool hit = false;
};

// Moves the arrow one tick along its line, swept so a fast arrow cannot tunnel through the target. It dies on a hit or
// when its range runs out. An out-of-range target position is a miss.
ProjectileResult projectileStep(Projectile& arrow, std::int32_t targetX, std::int32_t targetY, const BowTuning& tuning);

// Damage of one hit: base damage scaled by the draw, and by the opener bonus when it is the opening hit.
std::int32_t arrowDamage(std::int32_t drawPermille, bool opener, const BowTuning& tuning);

// Approach: nothing hit yet. Opened: the bow landed at least one hit. Closed: the target is in melee range, the bow's
// job is over for this encounter and the weapon should swap.
enum class OpeningPhase : std::uint8_t { Approach, Opened, Closed };

struct RangedOpening {
    std::uint8_t phase = 0;
    std::int32_t shots = 0;
    std::int32_t hits = 0;
    bool openerSpent = false;

    OpeningPhase openingPhase() const { return static_cast<OpeningPhase>(phase); }
};

void openingNoteShot(RangedOpening& opening);

// Records a hit and returns its damage. The opener bonus applies only to the first hit and only on an unaware target.
std::int32_t openingNoteHit(RangedOpening& opening, std::int32_t drawPermille, bool targetUnaware, const BowTuning& tuning);

// Closed is one way within an encounter: backing off does not reopen it. A negative distance counts as closed.
void openingStep(RangedOpening& opening, std::int32_t distanceMm, const BowTuning& tuning);

}
