#include "ichor/stage0/combat.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

const char* name(BrainState state)
{
    switch (state) {
    case BrainState::Unaware: return "Unaware";
    case BrainState::Approach: return "Approach";
    case BrainState::Neutral: return "Neutral";
    case BrainState::Tell: return "Tell";
    case BrainState::Punish: return "Punish";
    case BrainState::Committing: return "Committing";
    case BrainState::Rest: return "Rest";
    }
    return "?";
}

namespace {

void enter(SkirmisherBrain& brain, BrainState next)
{
    brain.state = static_cast<std::uint8_t>(next);
    brain.stateTime = 0.0f;
}

}

// A port of the Unreal Stage 0 skirmisher: a switch on an enum, desire that
// accumulates in the engagement band, a rest after three commits or six
// seconds, and a tell before every wind-up.
void Stage0::skirmisherSystem(Scene& scene, double dt)
{
    const float fdt = static_cast<float>(dt);
    const entt::entity foeEntity = scene.find(foe_);
    const entt::entity playerEntity = scene.find(player_);
    if (foeEntity == entt::null || playerEntity == entt::null || !scene.registry().all_of<SkirmisherBrain>(foeEntity)) {
        return;
    }
    auto& registry = scene.registry();
    SkirmisherBrain& brain = registry.get<SkirmisherBrain>(foeEntity);
    Combatant& combat = registry.get<Combatant>(foeEntity);
    CharacterBody& body = registry.get<CharacterBody>(foeEntity);
    const Transform& self = registry.get<Transform>(foeEntity);
    const Transform& target = registry.get<Transform>(playerEntity);
    const Combatant& targetCombat = registry.get<Combatant>(playerEntity);
    Rng& rng = scene.rng("stage0.skirmisher");

    if (!combat.alive()) {
        return;
    }
    if (combat.combatState() == CombatState::Staggered) {
        brain.desire = 0.0f;
        return;
    }
    brain.stateTime += fdt;

    const glm::vec3 toTarget(target.position - self.position);
    const glm::vec3 flat(toTarget.x, 0.0f, toTarget.z);
    const float distance = glm::length(flat);
    const glm::vec3 forward = distance > 1e-4f ? flat / distance : glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward);
    const float strikeRange = tuning_.attackRange + tuning_.attackRadius + tuning_.bodyRadius;

    const bool targetRecovering = targetCombat.combatState() == CombatState::Recovery;
    const bool targetStaggered = targetCombat.combatState() == CombatState::Staggered;
    if (!targetStaggered) {
        brain.followUps = 0;
    }
    const bool mustLetThemUp = targetStaggered && brain.followUps >= tuning_.maxFollowUpsIntoStagger;

    if (!targetCombat.alive()) {
        // A dead target is not an opponent.
        if (brain.brainState() != BrainState::Unaware) enter(brain, BrainState::Unaware);
        brain.desire = 0.0f;
        brain.commits = 0;
        brain.pressure = 0.0f;
        body.intent = glm::vec3(0.0f);
        return;
    }

    glm::vec3 move(0.0f);
    const float speedScale = combat.committed() ? 0.15f : 1.0f;
    const auto moveToward = [&](glm::vec3 dir, float scale) { move += dir * scale; };

    switch (brain.brainState()) {
    case BrainState::Unaware:
        if (distance <= tuning_.aggroRange) enter(brain, BrainState::Approach);
        break;

    case BrainState::Approach:
        moveToward(forward, 1.0f);
        if (distance <= tuning_.preferredSpacing + tuning_.reCommitTolerance) enter(brain, BrainState::Neutral);
        break;

    case BrainState::Neutral: {
        // Hold the band and circle; desire builds rather than a timer firing.
        const float error = distance - tuning_.preferredSpacing;
        if (std::abs(error) > 0.4f) moveToward(forward * (error > 0.0f ? 1.0f : -1.0f), 0.8f);
        moveToward(right * brain.strafeSign, tuning_.strafeSpeedScale);
        if (std::fmod(brain.stateTime, 2.2f) < fdt) brain.strafeSign = -brain.strafeSign;

        brain.pressure += fdt;
        brain.desire += (targetRecovering ? tuning_.desireGainVsRecovering : tuning_.desireGainRate) * fdt;

        if (targetRecovering && distance <= tuning_.preferredSpacing + tuning_.reCommitTolerance) {
            enter(brain, BrainState::Punish);
            break;
        }
        if (brain.commits >= tuning_.restAfterCommits || brain.pressure >= tuning_.restAfterSeconds) {
            enter(brain, BrainState::Rest);
            break;
        }
        if (brain.desire >= brain.nextThreshold) {
            if (distance <= strikeRange && !mustLetThemUp) {
                if (tuning_.tellCue != kNoSound) {
                    scene.emitSound({foe_, tuning_.tellCue, self.position});
                }
                if (targetStaggered) brain.followUps += 1;
                enter(brain, BrainState::Tell);
            } else {
                moveToward(forward, 1.0f);
            }
        }
        break;
    }

    case BrainState::Tell:
        // A weight shift before the wind-up proper: the cheapest fairness upgrade there is.
        if (brain.stateTime >= tuning_.tellDuration) {
            const AttackKind kind = rng.nextFloat() < tuning_.heavyChance ? AttackKind::Heavy : AttackKind::Light;
            // Out of stamina is not a commit: back to neutral, desire kept, so it tries again soon.
            enter(brain, tryAttack(scene, foeEntity, kind) ? BrainState::Committing : BrainState::Neutral);
        }
        break;

    case BrainState::Punish:
        if (distance > strikeRange * 0.8f) moveToward(forward, 1.0f);
        if (brain.stateTime >= tuning_.punishReactionTime) {
            enter(brain, tryAttack(scene, foeEntity, AttackKind::Light) ? BrainState::Committing : BrainState::Neutral);
        }
        break;

    case BrainState::Committing:
        // Wait out the swing. No cancelling, no repositioning mid-attack.
        if (!combat.committed()) {
            brain.desire = 0.0f;
            brain.commits += 1;
            // Reset with jitter so the pressure cannot be counted.
            brain.nextThreshold = tuning_.desireThreshold + (rng.nextFloat() * 2.0f - 1.0f) * tuning_.desireJitter;
            enter(brain, BrainState::Neutral);
        }
        break;

    case BrainState::Rest:
        // Deliberate disengagement: where the player heals, repositions and thinks.
        if (distance < tuning_.preferredSpacing * 1.6f) moveToward(-forward, 0.7f);
        if (brain.stateTime >= tuning_.restDuration) {
            brain.commits = 0;
            brain.pressure = 0.0f;
            brain.desire = 0.0f;
            enter(brain, BrainState::Neutral);
        }
        break;
    }

    if (combat.committed() || combat.combatState() == CombatState::Staggered) {
        body.intent = glm::vec3(0.0f);
    } else {
        const float len = glm::length(move);
        body.intent = len > 1e-4f ? move / std::max(len, 1.0f) * tuning_.walkSpeed * speedScale : glm::vec3(0.0f);
    }
}

}
