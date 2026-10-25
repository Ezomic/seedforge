#include "ichor/combat_bridge/role_bot.h"

#include "ichor/sim/components.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr double kTicksPerSecond = 60.0;
constexpr float kReachFraction = 0.9f;  // of the weapon reach: close enough to land the blow
constexpr float kSlamMargin = 0.4f;     // metres outside the slam radius that counts as clear of it
constexpr float kRetreatMetres = 1.1f;  // from striking distance to clear of the slam
constexpr int kSpareTicks = 6;
constexpr int kFarTicks = 1 << 20;
constexpr float kBandSlackM = 3.0f; // how far inside the artillery's range the bot waits

InputFrame steer(InputFrame frame, glm::vec2 worldXz)
{
    frame.moveX = worldXz.x;
    frame.moveY = -worldXz.y;
    return frame;
}

// Fixed steps until the planted anchor's slam lands if nothing breaks its poise, as far as a person watching can tell.
int ticksToSlam(const stage0::AnchorBrain& brain, const stage0::Combatant& combatant, const stage0::Tuning& tuning, const stage0::AnchorTuning& anchor)
{
    switch (brain.anchorState()) {
    case stage0::AnchorState::Windup: return std::max(anchor.windupTicks - brain.stateTicks, 0);
    case stage0::AnchorState::Recover: return std::max(anchor.recoveryTicks - brain.stateTicks, 0) + anchor.cooldownTicks + anchor.windupTicks;
    case stage0::AnchorState::Hold: return brain.cooldownTicks + anchor.windupTicks;
    case stage0::AnchorState::Advance: return anchor.windupTicks;
    case stage0::AnchorState::Staggered: {
        const double left = std::max(0.0, static_cast<double>(tuning.staggerDuration - combatant.stateTime));
        return static_cast<int>(left * kTicksPerSecond) + anchor.cooldownTicks + anchor.windupTicks;
    }
    case stage0::AnchorState::Unaware: return kFarTicks;
    }
    return 0;
}

}

InputFrame roleBotInput(Scene& scene, const stage0::Stage0& stage)
{
    InputFrame frame;
    auto& registry = scene.registry();
    const entt::entity meEntity = scene.find(stage.playerId());
    const entt::entity foeEntity = scene.find(stage.foeId());
    if (meEntity == entt::null || foeEntity == entt::null) return frame;
    const auto& me = registry.get<stage0::Combatant>(meEntity);
    const auto& foe = registry.get<stage0::Combatant>(foeEntity);
    if (!me.alive() || !foe.alive() || me.combatState() != stage0::CombatState::Idle) return frame;

    const stage0::Tuning& tuning = stage.tuning();
    const glm::dvec3 here = registry.get<Transform>(meEntity).position;
    const glm::dvec3 there = registry.get<Transform>(foeEntity).position;
    const double dx = there.x - here.x;
    const double dz = there.z - here.z;
    if (!(std::abs(dx) < 1.0e6 && std::abs(dz) < 1.0e6)) return frame;
    const glm::vec2 toFoe(static_cast<float>(dx), static_cast<float>(dz));
    const float distance = glm::length(toFoe);
    if (!std::isfinite(distance)) return frame;
    const glm::vec2 heading = distance > 1e-4f ? toFoe / distance : glm::vec2(0.0f, -1.0f);
    const float reach = (tuning.attackRange + tuning.attackRadius) * kReachFraction;
    const bool canSwing = me.stamina >= tuning.lightStaminaCost;

    const auto strikeOrClose = [&](bool sprint) {
        if (distance <= reach) {
            if (canSwing) frame.buttons |= InputFrame::Attack;
            return frame;
        }
        if (sprint) frame.buttons |= InputFrame::Sprint;
        return steer(frame, heading);
    };

    if (const auto* anchor = registry.try_get<stage0::AnchorBrain>(foeEntity)) {
        const stage0::AnchorTuning& t = stage.anchorTuning();
        const float clear = t.slamRadius + kSlamMargin;
        const float commit = static_cast<float>((tuning.lightWindup + tuning.lightActive + tuning.lightRecovery) * kTicksPerSecond);
        const int needed = static_cast<int>(commit + kRetreatMetres / std::max(tuning.moveSpeed, 0.1f) * kTicksPerSecond) + kSpareTicks;
        const bool slamming = anchor->anchorState() == stage0::AnchorState::Windup;
        if (!slamming && ticksToSlam(*anchor, foe, tuning, t) >= needed) {
            return strikeOrClose(false);
        }
        if (distance < clear) return steer(frame, -heading);
        return frame;
    }

    if (const auto* artillery = registry.try_get<stage0::ArtilleryBrain>(foeEntity)) {
        // It only stands still to aim and to recover, and it backs away from anyone closer than its minimum range, so
        // chasing it never lands a blow. Waiting inside its band makes it aim; then the player runs in off the mark.
        const stage0::ArtilleryState state = artillery->artilleryState();
        const bool exposed = state == stage0::ArtilleryState::Aim || state == stage0::ArtilleryState::Recover || state == stage0::ArtilleryState::Staggered;
        if (exposed || distance <= reach) return strikeOrClose(true);
        if (distance > stage.artilleryTuning().maxRange - kBandSlackM) return steer(frame, heading);
        return frame;
    }
    return strikeOrClose(false);
}

}
