#include "ichor/stage0/attack_rules.h"

#include <algorithm>

namespace ichor::stage0 {

namespace {
constexpr std::int32_t kHalfTurnMdeg = kFullTurnMdeg / 2;
constexpr int kTickCap = 1 << 24;
} // namespace

std::int32_t normalizeMdeg(std::int64_t mdeg)
{
    std::int64_t r = mdeg % kFullTurnMdeg;
    if (r < 0) r += kFullTurnMdeg;
    return static_cast<std::int32_t>(r);
}

std::int32_t shortestArcMdeg(std::int32_t from, std::int32_t to)
{
    std::int32_t d = normalizeMdeg(static_cast<std::int64_t>(to) - from);
    if (d > kHalfTurnMdeg) d -= kFullTurnMdeg;
    return d;
}

std::int32_t clampedFacing(AttackPhase phase, std::int32_t current, std::int32_t desired, const RotationLimits& limits)
{
    std::int32_t limit;
    switch (phase) {
    case AttackPhase::Windup: limit = limits.windupMdegPerTick; break;
    case AttackPhase::Active: limit = limits.activeMdegPerTick; break;
    default: return normalizeMdeg(desired);
    }
    limit = std::max(limit, 0);
    const std::int32_t arc = shortestArcMdeg(current, desired);
    return normalizeMdeg(static_cast<std::int64_t>(current) + std::clamp(arc, -limit, limit));
}

void tickGate(AttackGate& gate, const CommitInput& in, const CommitRules& rules)
{
    if (!in.playerStaggered) gate.followUpsThisStagger = 0;
    if (in.distanceMm >= rules.spacingMinMm) {
        gate.ticksAtSpacing = std::min(gate.ticksAtSpacing + 1, kTickCap);
    } else {
        gate.ticksAtSpacing = 0;
    }
}

CommitVeto canCommit(const AttackGate& gate, const CommitInput& in, const CommitRules& rules)
{
    if (!in.attackerOnScreen && !in.audioTelegraphPlays) return CommitVeto::OffscreenSilent;
    if (in.playerStaggered && gate.followUpsThisStagger >= rules.maxFollowUpsOnStagger) return CommitVeto::StaggerChain;
    if (gate.hasAttacked && gate.ticksAtSpacing < rules.spacingBeatTicks) return CommitVeto::NotSpaced;
    return CommitVeto::None;
}

void noteCommit(AttackGate& gate, const CommitInput& in)
{
    gate.hasAttacked = true;
    gate.ticksAtSpacing = 0;
    if (in.playerStaggered) gate.followUpsThisStagger = std::min(gate.followUpsThisStagger + 1, kTickCap);
}

} // namespace ichor::stage0
