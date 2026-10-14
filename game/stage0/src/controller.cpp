#include "ichor/stage0/combat.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

const char* name(ControllerState state)
{
    switch (state) {
    case ControllerState::Unaware: return "Unaware";
    case ControllerState::Reposition: return "Reposition";
    case ControllerState::Cast: return "Cast";
    }
    return "?";
}

namespace {

void enter(ControllerBrain& brain, ControllerState next)
{
    brain.state = static_cast<std::uint8_t>(next);
    brain.stateTicks = 0;
}

}

// The second foe role: keeps its distance, slows the target from range with a
// telegraphed cast, and recasts once the slow runs low. Every timer counts whole
// ticks. A stagger interrupts a cast, which is the reason to rush it.
void Stage0::controllerSystem(Scene& scene)
{
    const entt::entity foeEntity = scene.find(foe_);
    const entt::entity playerEntity = scene.find(player_);
    if (foeEntity == entt::null || playerEntity == entt::null || !scene.registry().all_of<ControllerBrain>(foeEntity)) {
        return;
    }
    auto& registry = scene.registry();
    ControllerBrain& brain = registry.get<ControllerBrain>(foeEntity);
    Combatant& combat = registry.get<Combatant>(foeEntity);
    CharacterBody& body = registry.get<CharacterBody>(foeEntity);
    const Transform& self = registry.get<Transform>(foeEntity);
    const Transform& target = registry.get<Transform>(playerEntity);
    const Combatant& targetCombat = registry.get<Combatant>(playerEntity);

    // Loaded state is data: a negative cooldown or a runaway counter is pulled back in.
    brain.cooldownTicks = std::clamp(brain.cooldownTicks, 0, tuning_.controllerCooldownTicks);
    brain.stateTicks = std::clamp(brain.stateTicks, 0, 1 << 20);
    if (brain.state > static_cast<std::uint8_t>(ControllerState::Cast)) enter(brain, ControllerState::Unaware);
    if (!std::isfinite(brain.strafeSign) || brain.strafeSign == 0.0f) brain.strafeSign = 1.0f;

    if (!combat.alive() || combat.health <= 0.0f) {
        body.intent = glm::vec3(0.0f);
        return;
    }
    if (combat.combatState() == CombatState::Staggered) {
        if (brain.controllerState() == ControllerState::Cast) {
            brain.cooldownTicks = tuning_.controllerCooldownTicks;
            enter(brain, ControllerState::Reposition);
        }
        body.intent = glm::vec3(0.0f);
        return;
    }
    if (!targetCombat.alive()) {
        if (brain.controllerState() != ControllerState::Unaware) enter(brain, ControllerState::Unaware);
        body.intent = glm::vec3(0.0f);
        return;
    }

    const glm::vec3 flat(static_cast<float>(target.position.x - self.position.x), 0.0f, static_cast<float>(target.position.z - self.position.z));
    const float distance = glm::length(flat);
    if (!std::isfinite(distance)) {
        body.intent = glm::vec3(0.0f);
        return;
    }
    const glm::vec3 forward = distance > 1e-4f ? flat / distance : glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward);

    brain.stateTicks = std::min(brain.stateTicks + 1, 1 << 20);
    if (brain.cooldownTicks > 0) --brain.cooldownTicks;

    glm::vec3 move(0.0f);
    bool still = combat.committed();

    switch (brain.controllerState()) {
    case ControllerState::Unaware:
        if (distance <= tuning_.aggroRange) enter(brain, ControllerState::Reposition);
        break;

    case ControllerState::Reposition: {
        if (distance < tuning_.controllerRetreatBelow) {
            move -= forward;
        } else if (distance > tuning_.controllerCastRange) {
            move += forward;
        } else {
            move += right * brain.strafeSign * tuning_.controllerStrafeSpeedScale;
            if (brain.stateTicks % tuning_.controllerStrafeFlipTicks == 0) brain.strafeSign = -brain.strafeSign;
        }

        const StatusEffect* slow = targetCombat.status.find(StatusId::Slow);
        const bool slowHolds = slow != nullptr && slow->remainingTicks > tuning_.controllerRecastBelowTicks;
        if (brain.cooldownTicks == 0 && !slowHolds && distance <= tuning_.controllerCastRange && !combat.committed()) {
            recorder_.noteAttack(false, scene.time());
            if (tuning_.controllerCastCue != kNoSound) {
                scene.emitSound({foe_, tuning_.controllerCastCue, self.position});
            }
            enter(brain, ControllerState::Cast);
        }
        break;
    }

    case ControllerState::Cast:
        still = true;
        if (brain.stateTicks >= tuning_.controllerCastTicks) {
            brain.casts += 1;
            brain.cooldownTicks = tuning_.controllerCooldownTicks;
            if (distance <= tuning_.controllerCastRange + 1.0f && applyStatus(scene, playerEntity, StatusId::Slow)) {
                recorder_.noteHit(false, 0.0f, false);
            } else {
                recorder_.noteWhiff(false);
            }
            enter(brain, ControllerState::Reposition);
        }
        break;
    }

    const float len = glm::length(move);
    body.intent = still || len <= 1e-4f ? glm::vec3(0.0f) : move / std::max(len, 1.0f) * tuning_.walkSpeed * combat.status.moveScale();
}

}
