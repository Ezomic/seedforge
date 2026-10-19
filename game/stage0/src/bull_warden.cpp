#include "ichor/stage0/bull_warden.h"

#include "ichor/assets/cooker.h"
#include "ichor/core/log.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

namespace {

// The GLB is exported facing +Z and Stage 0 characters face -Z, so the root turns half a way round.
constexpr float kRootYawDegrees = 180.0f;

float clamp01(float x)
{
    return std::isnan(x) ? 0.0f : std::clamp(x, 0.0f, 1.0f);
}

}

bool BullWardenClips::complete() const
{
    const std::uint32_t all[] = {idle, walk, attack, stagger, death};
    return std::none_of(std::begin(all), std::end(all), [](std::uint32_t c) { return c == AnimationSet::kNoClip; });
}

bool cookBullWarden(const std::filesystem::path& folder, BullWardenKit& kit)
{
    CookStats stats;
    CookOptions options;
    options.rootYawDegrees = kRootYawDegrees;
    SceneBundle bundle;
    if (!cookGltf(folder / "SK_BullWarden.glb", bundle, stats, options)) return false;
    if (bundle.skeletons.size() != 1 || bundle.meshes.size() != 1) {
        logf(LogLevel::Error, "{}: the Bull-Warden needs one skeleton and one mesh", folder.string());
        return false;
    }
    // The clip indices are taken from the very set that will be sampled, so they stay valid however often the kit is reused.
    kit.animations.clear();
    kit.bundle = std::move(bundle);
    if (!kit.animations.load(kit.bundle)) return false;
    const AnimationSet& animations = kit.animations;
    const Skeleton& skeleton = kit.bundle.skeletons[0];
    BullWardenClips clips;
    clips.idle = animations.findClip("idle");
    clips.walk = animations.findClip("walk");
    clips.attack = animations.findClip("attack");
    clips.stagger = animations.findClip("stagger");
    clips.death = animations.findClip("death");
    if (!clips.complete()) {
        logf(LogLevel::Error, "{}: the Bull-Warden needs idle, walk, attack, stagger and death clips", folder.string());
        return false;
    }
    clips.idleDuration = animations.clipDuration(clips.idle);
    clips.walkDuration = animations.clipDuration(clips.walk);
    clips.attackDuration = animations.clipDuration(clips.attack);
    clips.staggerDuration = animations.clipDuration(clips.stagger);
    clips.deathDuration = animations.clipDuration(clips.death);
    const auto isProp = [](const std::string& name) {
        for (const char* prop : {"Axe", "Shield", "Hand"}) {
            if (name.find(prop) != std::string::npos) return true;
        }
        return false;
    };
    kit.bodyJoints.clear();
    kit.groundClearance = 1e9f;
    for (std::uint32_t j = 0; j < skeleton.jointNames.size(); ++j) {
        if (isProp(skeleton.jointNames[j])) continue;
        kit.bodyJoints.push_back(j);
        kit.groundClearance = std::min(kit.groundClearance, glm::inverse(skeleton.inverseBind[j])[3].y);
    }
    if (kit.bodyJoints.empty()) return false;
    kit.clips = clips;
    return true;
}

BullWardenPose bullWardenPose(const Combatant& c, const Tuning& tuning, const BullWardenClips& clips, const BullWardenSwing& swing, bool moving, double clock)
{
    const float t = std::isnan(c.stateTime) ? 0.0f : std::max(c.stateTime, 0.0f); // +inf is a long time, not none
    const float strikeStart = clamp01(swing.strikeStart);
    const float strikeEnd = std::max(clamp01(swing.strikeEnd), strikeStart);
    const auto loop = [&](std::uint32_t clip, float duration) {
        const float d = std::max(duration, 1e-3f);
        return BullWardenPose{clip, static_cast<float>(std::fmod(std::isfinite(clock) ? std::max(clock, 0.0) : 0.0, static_cast<double>(d)))};
    };
    switch (c.combatState()) {
    case CombatState::Windup:
    case CombatState::Active:
    case CombatState::Recovery: {
        const bool heavy = c.attack == static_cast<std::uint8_t>(AttackKind::Heavy);
        const float windup = std::max(heavy ? tuning.heavyWindup : tuning.lightWindup, 1e-3f);
        const float active = std::max(heavy ? tuning.heavyActive : tuning.lightActive, 1e-3f);
        const float recovery = std::max(heavy ? tuning.heavyRecovery : tuning.lightRecovery, 1e-3f);
        float f = 0.0f;
        if (c.combatState() == CombatState::Windup) f = strikeStart * clamp01(t / windup);
        else if (c.combatState() == CombatState::Active) f = strikeStart + (strikeEnd - strikeStart) * clamp01(t / active);
        else f = strikeEnd + (1.0f - strikeEnd) * clamp01(t / recovery);
        return {clips.attack, f * clips.attackDuration};
    }
    case CombatState::Staggered:
        return {clips.stagger, clamp01(t / std::max(tuning.staggerDuration, 1e-3f)) * clips.staggerDuration};
    case CombatState::Dead:
        return {clips.death, std::min(t, clips.deathDuration)};
    case CombatState::Dodging:
        return loop(clips.walk, clips.walkDuration);
    case CombatState::Idle:
        break;
    }
    return moving ? loop(clips.walk, clips.walkDuration) : loop(clips.idle, clips.idleDuration);
}

bool bullWardenFrame(const BullWardenKit& kit, const BullWardenPose& pose, BullWardenFrame& out)
{
    std::vector<glm::mat4> model;
    if (!kit.animations.samplePose(pose.clip, pose.time, model)) return false;
    const std::vector<glm::mat4>& inverseBind = kit.bundle.skeletons[0].inverseBind;
    if (model.size() != inverseBind.size()) return false;
    out.skin.resize(model.size());
    for (std::size_t j = 0; j < model.size(); ++j) out.skin[j] = model[j] * inverseBind[j];
    float lowest = 1e9f;
    for (const std::uint32_t j : kit.bodyJoints) lowest = std::min(lowest, model[j][3].y);
    out.lift = std::isfinite(lowest) ? kit.groundClearance - lowest : 0.0f;
    return true;
}

void appendBullWarden(const Scene& scene, StableId foe, double alpha, const Tuning& tuning, const BullWardenKit& kit, RenderView& view)
{
    const entt::entity entity = scene.find(foe);
    if (entity == entt::null) return;
    const auto* transform = scene.registry().try_get<Transform>(entity);
    const auto* previous = scene.registry().try_get<PreviousTransform>(entity);
    const auto* combatant = scene.registry().try_get<Combatant>(entity);
    if (transform == nullptr || previous == nullptr || combatant == nullptr) return;
    const glm::dvec3 feet = glm::mix(previous->position, transform->position, alpha);
    if (!std::isfinite(feet.x + feet.y + feet.z) || !std::isfinite(kit.scale)) return;
    const bool moving = glm::dot(transform->position - previous->position, transform->position - previous->position) > 1e-8;
    const BullWardenPose pose = bullWardenPose(*combatant, tuning, kit.clips, kit.swing, moving, scene.time());

    BullWardenFrame frame;
    if (!bullWardenFrame(kit, pose, frame)) return;
    const float yaw = std::isfinite(combatant->yaw) ? combatant->yaw : 0.0f;
    RenderInstance instance;
    instance.mesh = kit.mesh;
    instance.position = feet + glm::dvec3(0.0, static_cast<double>(frame.lift * kit.scale), 0.0);
    instance.basis = glm::mat3_cast(glm::angleAxis(-yaw, glm::vec3(0.0f, 1.0f, 0.0f))) * kit.scale;
    instance.jointOffset = static_cast<std::uint32_t>(view.jointPalette.size());
    instance.jointCount = static_cast<std::uint32_t>(frame.skin.size());
    view.jointPalette.insert(view.jointPalette.end(), frame.skin.begin(), frame.skin.end());
    view.instances.push_back(instance);
}

}
