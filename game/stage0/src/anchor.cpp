#include "ichor/stage0/anchor.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

const char* name(AnchorState state)
{
    switch (state) {
    case AnchorState::Unaware: return "Unaware";
    case AnchorState::Advance: return "Advance";
    case AnchorState::Windup: return "Windup";
    case AnchorState::Recover: return "Recover";
    case AnchorState::Hold: return "Hold";
    case AnchorState::Staggered: return "Staggered";
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

void enter(AnchorBrain& brain, AnchorState next)
{
    brain.state = static_cast<std::uint8_t>(next);
    brain.stateTicks = 0;
}

void beginWindup(AnchorBrain& brain, AnchorIntent& intent)
{
    enter(brain, AnchorState::Windup);
    intent.telegraph = true;
}

}

AnchorIntent anchorStep(AnchorBrain& brain, const AnchorSenses& senses, const AnchorTuning& tuning)
{
    AnchorIntent intent;
    if (!finite(senses.self) || !finite(senses.target)) {
        return intent;
    }

    brain.stateTicks = std::clamp(brain.stateTicks, 0, kMaxTicks);
    brain.cooldownTicks = std::clamp(brain.cooldownTicks, 0, std::max(tuning.cooldownTicks, 0));
    if (brain.state > static_cast<std::uint8_t>(AnchorState::Staggered)) enter(brain, AnchorState::Unaware);

    if (!senses.targetAlive) {
        if (brain.anchorState() != AnchorState::Unaware) enter(brain, AnchorState::Unaware);
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

    if (senses.staggered) {
        // A broken poise cancels whatever was committed; the cooldown starts when it ends.
        if (brain.anchorState() != AnchorState::Staggered) enter(brain, AnchorState::Staggered);
        return intent;
    }
    if (brain.anchorState() == AnchorState::Staggered) {
        brain.cooldownTicks = std::max(tuning.cooldownTicks - 1, 0);
        enter(brain, AnchorState::Hold);
    }

    const auto walk = [&]() {
        if (distance <= 1e-4f) return;
        const float speed = std::min(tuning.walkSpeed, distance / tuning.tickSeconds);
        intent.move = glm::vec2(static_cast<float>(dx / distance), static_cast<float>(dy / distance)) * speed;
    };

    switch (brain.anchorState()) {
    case AnchorState::Unaware:
        if (distance <= tuning.aggroRange) enter(brain, AnchorState::Advance);
        break;

    case AnchorState::Advance:
        if (distance <= tuning.slamRange) {
            if (brain.cooldownTicks == 0) {
                beginWindup(brain, intent);
            } else {
                enter(brain, AnchorState::Hold);
            }
        } else {
            walk();
        }
        break;

    case AnchorState::Hold:
        // Planted: it controls the ground it stands on and does not chase.
        if (brain.cooldownTicks == 0) {
            if (distance <= tuning.slamRange) {
                beginWindup(brain, intent);
            } else {
                enter(brain, AnchorState::Advance);
            }
        }
        break;

    case AnchorState::Windup:
        if (brain.stateTicks >= tuning.windupTicks) {
            intent.slam = true;
            intent.reaches = distance <= tuning.slamRadius;
            brain.slams += 1;
            enter(brain, AnchorState::Recover);
        }
        break;

    case AnchorState::Recover:
        if (brain.stateTicks >= tuning.recoveryTicks) {
            brain.cooldownTicks = std::max(tuning.cooldownTicks, 0);
            enter(brain, AnchorState::Hold);
        }
        break;

    case AnchorState::Staggered:
        break;
    }
    return intent;
}

}
