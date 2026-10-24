#include "ichor/combat_bridge/role_view.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr float kAnchorScale = 1.3f;
constexpr float kArtilleryScale = 0.8f;
constexpr double kRingRadiusM = 1.0;
constexpr int kRingSegments = 20;

float fraction(std::int32_t ticks, int total)
{
    return std::clamp(static_cast<float>(ticks) / static_cast<float>(std::max(total, 1)), 0.0f, 1.0f);
}

// The combat state the Bull-Warden's clips are driven by. The roles keep their own brains and leave the combatant idle,
// so the wind-up and the recovery are mapped onto the heavy swing of the clip.
stage0::Combatant poseSource(const Scene& scene, entt::entity foe, const stage0::Stage0& stage)
{
    const auto& registry = scene.registry();
    stage0::Combatant pose = registry.get<stage0::Combatant>(foe);
    if (!pose.alive() || pose.combatState() != stage0::CombatState::Idle) return pose;
    const stage0::Tuning& tuning = stage.tuning();
    pose.attack = static_cast<std::uint8_t>(stage0::AttackKind::Heavy);
    const auto windup = [&](float progress) {
        pose.state = static_cast<std::uint8_t>(stage0::CombatState::Windup);
        pose.stateTime = progress * tuning.heavyWindup;
    };
    const auto recovery = [&](float progress) {
        pose.state = static_cast<std::uint8_t>(stage0::CombatState::Recovery);
        pose.stateTime = progress * tuning.heavyRecovery;
    };
    if (const auto* anchor = registry.try_get<stage0::AnchorBrain>(foe)) {
        if (anchor->anchorState() == stage0::AnchorState::Windup) windup(fraction(anchor->stateTicks, stage.anchorTuning().windupTicks));
        else if (anchor->anchorState() == stage0::AnchorState::Recover) recovery(fraction(anchor->stateTicks, stage.anchorTuning().recoveryTicks));
    } else if (const auto* artillery = registry.try_get<stage0::ArtilleryBrain>(foe)) {
        if (artillery->artilleryState() == stage0::ArtilleryState::Aim) windup(fraction(artillery->stateTicks, stage.artilleryTuning().windupTicks));
        else if (artillery->artilleryState() == stage0::ArtilleryState::Recover) recovery(fraction(artillery->stateTicks, stage.artilleryTuning().recoveryTicks));
    }
    return pose;
}

void appendRing(glm::dvec3 centre, glm::vec3 color, RenderView& view)
{
    for (int i = 0; i < kRingSegments; ++i) {
        const double a0 = 6.283185307179586 * i / kRingSegments;
        const double a1 = 6.283185307179586 * (i + 1) / kRingSegments;
        view.lines.push_back({centre + glm::dvec3(std::cos(a0) * kRingRadiusM, 0.0, std::sin(a0) * kRingRadiusM),
            centre + glm::dvec3(std::cos(a1) * kRingRadiusM, 0.0, std::sin(a1) * kRingRadiusM), color});
    }
}

}

float roleScale(stage0::FoeRole role)
{
    switch (role) {
    case stage0::FoeRole::Anchor: return kAnchorScale;
    case stage0::FoeRole::Artillery: return kArtilleryScale;
    default: return 1.0f;
    }
}

void appendRoleFoe(const Scene& scene, const stage0::Stage0& stage, double alpha, const stage0::BullWardenKit& kit, RenderView& view)
{
    const stage0::FoeRole role = stage.foeRole();
    if (role != stage0::FoeRole::Anchor && role != stage0::FoeRole::Artillery) {
        stage0::appendBullWarden(scene, stage.foeId(), alpha, stage.tuning(), kit, view);
        return;
    }
    const entt::entity entity = scene.find(stage.foeId());
    if (entity == entt::null) return;
    const auto& registry = scene.registry();
    const auto* transform = registry.try_get<Transform>(entity);
    const auto* previous = registry.try_get<PreviousTransform>(entity);
    if (transform == nullptr || previous == nullptr || !registry.all_of<stage0::Combatant>(entity)) return;
    const glm::dvec3 feet = glm::mix(previous->position, transform->position, alpha);
    const float scale = kit.scale * roleScale(role);
    if (!std::isfinite(feet.x + feet.y + feet.z) || !std::isfinite(scale)) return;

    const stage0::Combatant source = poseSource(scene, entity, stage);
    const bool moving = glm::dot(transform->position - previous->position, transform->position - previous->position) > 1e-8;
    const stage0::BullWardenPose pose = stage0::bullWardenPose(source, stage.tuning(), kit.clips, kit.swing, moving, scene.time());
    stage0::BullWardenFrame frame;
    if (!stage0::bullWardenFrame(kit, pose, frame)) return;

    const float yaw = std::isfinite(source.yaw) ? source.yaw : 0.0f;
    RenderInstance instance;
    instance.mesh = kit.mesh;
    instance.position = feet + glm::dvec3(0.0, static_cast<double>(frame.lift * scale), 0.0);
    instance.basis = glm::mat3_cast(glm::angleAxis(-yaw, glm::vec3(0.0f, 1.0f, 0.0f))) * scale;
    instance.jointOffset = static_cast<std::uint32_t>(view.jointPalette.size());
    instance.jointCount = static_cast<std::uint32_t>(frame.skin.size());
    view.jointPalette.insert(view.jointPalette.end(), frame.skin.begin(), frame.skin.end());
    view.instances.push_back(instance);

    const glm::vec3 color = role == stage0::FoeRole::Anchor ? glm::vec3(1.0f, 0.55f, 0.1f) : glm::vec3(0.7f, 0.3f, 1.0f);
    appendRing(feet + glm::dvec3(0.0, 0.08, 0.0), color, view);
}

}
