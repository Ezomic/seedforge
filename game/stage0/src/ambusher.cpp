#include "ichor/stage0/ambusher.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

const char* name(AmbusherState state)
{
    switch (state) {
    case AmbusherState::Hidden: return "Hidden";
    case AmbusherState::Rush: return "Rush";
    case AmbusherState::Strike: return "Strike";
    case AmbusherState::Recover: return "Recover";
    case AmbusherState::Withdraw: return "Withdraw";
    }
    return "?";
}

namespace {

constexpr std::int32_t kMaxTicks = 1 << 20;

bool finite(glm::vec2 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y);
}

void enter(AmbusherBrain& brain, AmbusherState next)
{
    brain.state = static_cast<std::uint8_t>(next);
    brain.stateTicks = 0;
}

glm::vec2 walkToward(glm::vec2 from, glm::vec2 to, float speed, float tickSeconds)
{
    const glm::vec2 delta = to - from;
    const float len = glm::length(delta);
    if (!std::isfinite(len) || len <= 1e-4f || !(speed > 0.0f)) {
        return glm::vec2(0.0f);
    }
    const float step = tickSeconds > 0.0f ? std::min(speed, len / tickSeconds) : speed;
    return delta / len * step;
}

// Looking at the cover: close enough, and the facing points at it within the cone.
bool looksAtCover(const AmbusherSenses& senses, const AmbusherTuning& tuning)
{
    if (!finite(senses.targetFacing)) {
        return false;
    }
    const float facingLen = glm::length(senses.targetFacing);
    if (!(facingLen > 1e-4f) || !std::isfinite(facingLen)) {
        return false;
    }
    const glm::vec2 toCover = senses.cover - senses.target;
    const float distance = glm::length(toCover);
    if (!(distance <= tuning.spotRange)) {
        return false;
    }
    if (distance <= 1e-4f) {
        return true;
    }
    return glm::dot(senses.targetFacing / facingLen, toCover / distance) >= tuning.spotCosHalfCone;
}

void withdraw(AmbusherBrain& brain)
{
    enter(brain, AmbusherState::Withdraw);
}

}

AmbusherIntent ambusherStep(AmbusherBrain& brain, const AmbusherSenses& senses, const AmbusherTuning& tuning)
{
    AmbusherIntent intent;
    if (!finite(senses.self) || !finite(senses.cover) || !finite(senses.target)) {
        return intent;
    }

    // Loaded state is data: an unknown state or a runaway counter is pulled back in.
    if (brain.state > static_cast<std::uint8_t>(AmbusherState::Withdraw)) enter(brain, AmbusherState::Hidden);
    brain.stateTicks = std::clamp(brain.stateTicks, 0, kMaxTicks);
    brain.cooldownTicks = std::clamp(brain.cooldownTicks, 0, kMaxTicks);
    brain.spotTicks = std::clamp(brain.spotTicks, 0, std::max(tuning.spotTicksNeeded, 0));

    brain.stateTicks += 1;
    const float distance = glm::length(senses.target - senses.self);

    if (!senses.targetAlive && brain.ambusherState() != AmbusherState::Hidden && brain.ambusherState() != AmbusherState::Withdraw) {
        withdraw(brain);
    }

    switch (brain.ambusherState()) {
    case AmbusherState::Hidden: {
        if (brain.cooldownTicks > 0) brain.cooldownTicks -= 1;
        if (!senses.targetAlive) break;
        if (looksAtCover(senses, tuning)) {
            brain.spotTicks = std::min(brain.spotTicks + 1, std::max(tuning.spotTicksNeeded, 0));
            if (brain.spotTicks >= tuning.spotTicksNeeded) brain.concealed = false;
        } else {
            brain.spotTicks = std::max(brain.spotTicks - std::max(tuning.spotDecayTicks, 0), 0);
        }
        const float fromCover = glm::length(senses.target - senses.cover);
        if (brain.cooldownTicks == 0 && fromCover <= tuning.triggerRange) {
            brain.ambush = brain.concealed;
            enter(brain, AmbusherState::Rush);
        }
        break;
    }

    case AmbusherState::Rush:
        if (distance <= tuning.strikeRange) {
            enter(brain, AmbusherState::Strike);
        } else if (brain.stateTicks > tuning.rushMaxTicks) {
            withdraw(brain);
        } else {
            intent.move = walkToward(senses.self, senses.target, tuning.rushSpeed, tuning.tickSeconds);
        }
        break;

    case AmbusherState::Strike:
        // Committed: no cancelling, no repositioning.
        if (brain.stateTicks >= (brain.ambush ? tuning.ambushWindupTicks : tuning.windupTicks)) {
            intent.strike = true;
            intent.ambush = brain.ambush;
            brain.strikes += 1;
            enter(brain, AmbusherState::Recover);
        }
        break;

    case AmbusherState::Recover:
        if (brain.stateTicks >= tuning.recoveryTicks) withdraw(brain);
        break;

    case AmbusherState::Withdraw:
        if (glm::length(senses.cover - senses.self) <= tuning.arriveRadius) {
            brain.cooldownTicks = tuning.rearmTicks;
            brain.spotTicks = 0;
            brain.concealed = true;
            brain.ambush = true;
            enter(brain, AmbusherState::Hidden);
        } else {
            intent.move = walkToward(senses.self, senses.cover, tuning.withdrawSpeed, tuning.tickSeconds);
        }
        break;
    }
    return intent;
}

}
