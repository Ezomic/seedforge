#include "ichor/stage0/combat.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ichor::stage0 {

namespace {

// Standing eyes for the line of sight check; the controller bodies stand at their feet.
constexpr double kEyeHeightM = 1.5;

// Beyond this the brains ignore the target anyway; clamping first keeps the cast to float defined.
constexpr double kMaxCoordM = 1.0e9;

float narrow(double v)
{
    return std::isfinite(v) ? static_cast<float>(std::clamp(v, -kMaxCoordM, kMaxCoordM)) : std::numeric_limits<float>::quiet_NaN();
}

glm::vec2 flatOf(const Transform& transform)
{
    return {narrow(transform.position.x), narrow(transform.position.z)};
}

}

glm::vec3 Stage0::bodyIntent(glm::dvec3 at, glm::vec2 move, float scale) const
{
    if (!std::isfinite(move.x) || !std::isfinite(move.y) || !std::isfinite(scale)) return glm::vec3(0.0f);
    double pace = 1.0;
    if (ground_.pace) {
        const double asked = ground_.pace(at);
        pace = std::isfinite(asked) ? std::clamp(asked, 0.0, 1.0) : 1.0;
    }
    glm::dvec3 velocity(static_cast<double>(move.x) * scale * pace, 0.0, static_cast<double>(move.y) * scale * pace);
    if (ground_.filter) {
        const glm::dvec3 allowed = ground_.filter(at, velocity);
        velocity = std::isfinite(allowed.x) && std::isfinite(allowed.y) && std::isfinite(allowed.z) ? allowed : glm::dvec3(0.0);
    }
    return glm::vec3(velocity);
}

double Stage0::groundHeight(double x, double z, double fallback) const
{
    if (!ground_.height) return fallback;
    const double height = ground_.height(x, z);
    return std::isfinite(height) ? height : fallback;
}

// The Anchor: walks up slowly, plants, and slams an area after a fixed telegraph. A broken poise cancels the slam,
// which is the reason to hit it; the long recovery after a slam is the other answer.
void Stage0::anchorSystem(Scene& scene)
{
    const entt::entity foeEntity = scene.find(foe_);
    const entt::entity playerEntity = scene.find(player_);
    auto& registry = scene.registry();
    if (foeEntity == entt::null || playerEntity == entt::null || !registry.all_of<AnchorBrain>(foeEntity)) {
        return;
    }
    AnchorBrain& brain = registry.get<AnchorBrain>(foeEntity);
    Combatant& combat = registry.get<Combatant>(foeEntity);
    CharacterBody& body = registry.get<CharacterBody>(foeEntity);
    const Transform& self = registry.get<Transform>(foeEntity);
    const Transform& target = registry.get<Transform>(playerEntity);
    const Combatant& targetCombat = registry.get<Combatant>(playerEntity);

    body.intent = glm::vec3(0.0f);
    if (!combat.alive() || combat.health <= 0.0f) {
        return;
    }

    AnchorSenses senses;
    senses.self = flatOf(self);
    senses.target = flatOf(target);
    senses.targetAlive = targetCombat.alive();
    senses.staggered = combat.combatState() == CombatState::Staggered;
    const AnchorIntent intent = anchorStep(brain, senses, anchorTuning_);

    body.intent = bodyIntent(self.position, intent.move, combat.status.moveScale());
    if (intent.telegraph) {
        recorder_.noteAttack(false, scene.time());
        if (tuning_.heavyWindupCue != kNoSound) {
            scene.emitSound({foe_, tuning_.heavyWindupCue, self.position});
        }
    }
    if (intent.slam) {
        if (intent.reaches && targetCombat.alive()) {
            receiveHit(scene, playerEntity, foeEntity, anchorTuning_.slamDamage, anchorTuning_.slamPoiseDamage, attackerInView(scene, self.position));
        } else {
            recorder_.noteWhiff(false);
        }
    }
}

// The Artillery: holds a distance band, marks a ground point and lands a blast there after a fixed telegraph. Leaving
// the marked point, breaking line of sight before it aims, or closing in are the answers.
void Stage0::artillerySystem(Scene& scene)
{
    const entt::entity foeEntity = scene.find(foe_);
    const entt::entity playerEntity = scene.find(player_);
    auto& registry = scene.registry();
    if (foeEntity == entt::null || playerEntity == entt::null || !registry.all_of<ArtilleryBrain>(foeEntity)) {
        return;
    }
    ArtilleryBrain& brain = registry.get<ArtilleryBrain>(foeEntity);
    Combatant& combat = registry.get<Combatant>(foeEntity);
    CharacterBody& body = registry.get<CharacterBody>(foeEntity);
    const Transform& self = registry.get<Transform>(foeEntity);
    const Transform& target = registry.get<Transform>(playerEntity);
    const Combatant& targetCombat = registry.get<Combatant>(playerEntity);

    body.intent = glm::vec3(0.0f);
    if (!combat.alive() || combat.health <= 0.0f) {
        return;
    }

    ArtillerySenses senses;
    senses.self = flatOf(self);
    senses.target = flatOf(target);
    senses.targetAlive = targetCombat.alive();
    senses.staggered = combat.combatState() == CombatState::Staggered;
    senses.lineOfSight = !ground_.lineOfSight || ground_.lineOfSight(self.position + glm::dvec3(0.0, kEyeHeightM, 0.0), target.position + glm::dvec3(0.0, kEyeHeightM, 0.0));
    const ArtilleryIntent intent = artilleryStep(brain, senses, artilleryTuning_);

    body.intent = bodyIntent(self.position, intent.move, combat.status.moveScale());
    if (intent.telegraph) {
        recorder_.noteAttack(false, scene.time());
        if (tuning_.controllerCastCue != kNoSound) {
            scene.emitSound({foe_, tuning_.controllerCastCue, self.position});
        }
    }
    if (intent.fire) {
        if (intent.hits && targetCombat.alive()) {
            receiveHit(scene, playerEntity, foeEntity, artilleryTuning_.blastDamage, artilleryTuning_.blastPoiseDamage, attackerInView(scene, self.position));
        } else {
            recorder_.noteWhiff(false);
        }
    }
}

}
