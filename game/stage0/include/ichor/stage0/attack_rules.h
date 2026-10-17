#pragma once

#include <cstdint>

namespace ichor::stage0 {

// Doc 18 anti-frustration rules as pure integer logic: the rotation clamp and the commit gate.
// Angles are millidegrees, distances are millimetres, time is 60 Hz fixed steps; no floats, no libm.
// Wiring these into the enemy update loops is deliberately not here.

constexpr std::int32_t kFullTurnMdeg = 360000;

enum class AttackPhase : std::uint8_t { Neutral, Tell, Windup, Active, Recovery };

struct RotationLimits {
    std::int32_t windupMdegPerTick = 500; // 30 deg/s at 60 Hz
    std::int32_t activeMdegPerTick = 0;   // frozen while the hit is live
};

// Wraps any angle into [0, 360000).
std::int32_t normalizeMdeg(std::int64_t mdeg);

// Signed shortest arc from `from` to `to`, in (-180000, 180000].
std::int32_t shortestArcMdeg(std::int32_t from, std::int32_t to);

// One tick of facing. Windup and Active turn at most their limit toward `desired`; other phases
// track freely. Negative limits are treated as zero.
std::int32_t clampedFacing(AttackPhase phase, std::int32_t current, std::int32_t desired, const RotationLimits& limits = {});

enum class CommitVeto : std::uint8_t { None, OffscreenSilent, StaggerChain, NotSpaced };

struct CommitRules {
    std::int64_t spacingMinMm = 3000;  // must have backed off to at least this far from the player
    int spacingBeatTicks = 30;         // and held it this long after the last attack ended
    int maxFollowUpsOnStagger = 1;     // attacks allowed while the player stays staggered
};

struct CommitInput {
    bool attackerOnScreen = true;
    bool audioTelegraphPlays = false;
    bool playerStaggered = false;
    std::int64_t distanceMm = 0;
};

struct AttackGate {
    int ticksAtSpacing = 0;       // consecutive ticks at spacing since the last attack ended
    int followUpsThisStagger = 0; // attacks committed during the current player stagger
    bool hasAttacked = false;     // false until the first attack, so the opener is not delayed
};

// Call once per tick while not mid-attack.
void tickGate(AttackGate& gate, const CommitInput& in, const CommitRules& rules);

CommitVeto canCommit(const AttackGate& gate, const CommitInput& in, const CommitRules& rules);

// Call when a commit is accepted. Resets the spacing beat and counts stagger follow-ups.
void noteCommit(AttackGate& gate, const CommitInput& in);

} // namespace ichor::stage0
