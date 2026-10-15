#include "ichor/stage0/artillery.h"

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

const char* name(ArtilleryState state)
{
    switch (state) {
    case ArtilleryState::Unaware: return "Unaware";
    case ArtilleryState::Reposition: return "Reposition";
    case ArtilleryState::Aim: return "Aim";
    case ArtilleryState::Recover: return "Recover";
    case ArtilleryState::Staggered: return "Staggered";
    }
    return "?";
}

namespace {

constexpr std::int32_t kMaxTicks = 1 << 20;
constexpr double kMaxSpan = 1.0e6;

bool finite(glm::vec2 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y);
}

void enter(ArtilleryBrain& brain, ArtilleryState next)
{
    brain.state = static_cast<std::uint8_t>(next);
    brain.stateTicks = 0;
}

}

ArtilleryIntent artilleryStep(ArtilleryBrain& brain, const ArtillerySenses& senses, const ArtilleryTuning& tuning)
{
    ArtilleryIntent intent;

    brain.stateTicks = std::clamp(brain.stateTicks, 0, kMaxTicks);
    brain.cooldownTicks = std::clamp(brain.cooldownTicks, 0, std::max(tuning.cooldownTicks, 0));
    if (!std::isfinite(brain.aimX) || !std::isfinite(brain.aimY)) brain.aimX = brain.aimY = 0.0f;
    if (brain.state > static_cast<std::uint8_t>(ArtilleryState::Staggered)) enter(brain, ArtilleryState::Unaware);

    if (!senses.targetAlive) {
        if (brain.artilleryState() != ArtilleryState::Unaware) enter(brain, ArtilleryState::Unaware);
        return intent;
    }

    if (senses.staggered) {
        // A broken poise cancels the aim, even on a tick with unusable positions.
        if (brain.artilleryState() != ArtilleryState::Staggered) enter(brain, ArtilleryState::Staggered);
        return intent;
    }
    if (!finite(senses.self) || !finite(senses.target)) {
        return intent;
    }

    const double dx = static_cast<double>(senses.target.x) - static_cast<double>(senses.self.x);
    const double dy = static_cast<double>(senses.target.y) - static_cast<double>(senses.self.y);
    if (!(std::abs(dx) <= kMaxSpan && std::abs(dy) <= kMaxSpan)) {
        return intent;
    }
    const float distance = static_cast<float>(std::sqrt(dx * dx + dy * dy));

    brain.stateTicks = std::min(brain.stateTicks + 1, kMaxTicks);
    if (brain.cooldownTicks > 0) --brain.cooldownTicks;

    if (brain.artilleryState() == ArtilleryState::Staggered) {
        brain.cooldownTicks = std::max(tuning.cooldownTicks - 1, 0);
        enter(brain, ArtilleryState::Reposition);
    }

    const auto toward = [&](float speed) {
        if (distance <= 1e-4f) return glm::vec2(0.0f);
        return glm::vec2(static_cast<float>(dx / distance), static_cast<float>(dy / distance)) * speed;
    };

    switch (brain.artilleryState()) {
    case ArtilleryState::Unaware:
        if (distance <= tuning.aggroRange) enter(brain, ArtilleryState::Reposition);
        break;

    case ArtilleryState::Reposition:
        if (distance < tuning.minRange) {
            intent.move = -toward(tuning.retreatSpeed);
        } else if (!senses.lineOfSight || distance > tuning.maxRange) {
            intent.move = toward(std::min(tuning.walkSpeed, distance / tuning.tickSeconds));
        } else if (brain.cooldownTicks == 0) {
            brain.aimX = senses.target.x;
            brain.aimY = senses.target.y;
            intent.telegraph = true;
            intent.aimAt = senses.target;
            enter(brain, ArtilleryState::Aim);
        }
        break;

    case ArtilleryState::Aim:
        intent.aimAt = glm::vec2(brain.aimX, brain.aimY);
        if (brain.stateTicks >= tuning.windupTicks) {
            const double ax = static_cast<double>(senses.target.x) - static_cast<double>(brain.aimX);
            const double ay = static_cast<double>(senses.target.y) - static_cast<double>(brain.aimY);
            intent.fire = true;
            intent.hits = std::sqrt(ax * ax + ay * ay) <= static_cast<double>(tuning.blastRadius);
            brain.shots += 1;
            enter(brain, ArtilleryState::Recover);
        }
        break;

    case ArtilleryState::Recover:
        if (brain.stateTicks >= tuning.recoveryTicks) {
            brain.cooldownTicks = std::max(tuning.cooldownTicks, 0);
            enter(brain, ArtilleryState::Reposition);
        }
        break;

    case ArtilleryState::Staggered:
        break;
    }
    return intent;
}

}
