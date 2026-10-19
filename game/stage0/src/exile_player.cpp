#include "ichor/stage0/exile_player.h"

#include "ichor/core/log.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

namespace {

float fraction(float part, float whole)
{
    if (std::isnan(part)) return 0.0f;
    return whole > 0.0f ? std::clamp(part / whole, 0.0f, 1.0f) : 1.0f;
}

}

const char* name(ExileClip clip)
{
    switch (clip) {
    case ExileClip::Idle: return "idle";
    case ExileClip::Walk: return "walk";
    case ExileClip::Run: return "run";
    case ExileClip::Thrust: return "thrust";
    case ExileClip::Hit: return "hit";
    case ExileClip::Swing: return "swing";
    case ExileClip::Block: return "block";
    case ExileClip::Dodge: return "dodge";
    case ExileClip::Death: return "death";
    case ExileClip::Count: break;
    }
    return "idle";
}

ExileClipChoice chooseExileClip(const Combatant& c, const Tuning& tuning, float planarSpeed, float fixedDt)
{
    ExileClipChoice out;
    const bool heavy = c.attack == static_cast<std::uint8_t>(AttackKind::Heavy);
    const float windup = heavy ? tuning.heavyWindup : tuning.lightWindup;
    const float active = heavy ? tuning.heavyActive : tuning.lightActive;
    const float recovery = heavy ? tuning.heavyRecovery : tuning.lightRecovery;
    switch (c.combatState()) {
    case CombatState::Windup:
    case CombatState::Active:
    case CombatState::Recovery: {
        const float before = c.combatState() == CombatState::Windup ? 0.0f : c.combatState() == CombatState::Active ? windup : windup + active;
        out.clip = heavy ? ExileClip::Swing : ExileClip::Thrust;
        out.progress = fraction(before + c.stateTime, windup + active + recovery);
        return out;
    }
    case CombatState::Dodging:
        out.clip = ExileClip::Dodge;
        out.progress = fraction(c.stateTime, tuning.dodgeDuration);
        return out;
    case CombatState::Staggered:
        out.clip = ExileClip::Hit;
        out.progress = fraction(c.stateTime, tuning.staggerDuration);
        return out;
    case CombatState::Dead:
        out.clip = ExileClip::Death;
        out.progress = fraction(c.stateTime, kExileDeathSeconds);
        return out;
    case CombatState::Idle:
        break;
    }
    if (c.blocking) {
        out.clip = ExileClip::Block;
        out.progress = fraction(static_cast<float>(std::max(c.blockTicks, 0)) * fixedDt, kExileGuardRaiseSeconds);
        return out;
    }
    out.locomotion = true;
    const float speed = std::isfinite(planarSpeed) ? planarSpeed : 0.0f;
    out.clip = speed < kExileStandBelow ? ExileClip::Idle : speed < kExileWalkBelow ? ExileClip::Walk : ExileClip::Run;
    return out;
}

float exileLocomotionRate(float speed, float clipTravelSpeed)
{
    if (!std::isfinite(speed) || !std::isfinite(clipTravelSpeed) || clipTravelSpeed < 0.05f) return 1.0f;
    return std::clamp(speed / clipTravelSpeed, 0.4f, 1.6f);
}

bool ExilePlayer::load(const std::filesystem::path& folder, Renderer& renderer)
{
    if (!prepare(folder, renderer.meshCount())) return false;
    for (const SceneBundle& bundle : kit_.bundles) {
        if (!renderer.uploadBundle(bundle)) {
            loaded_ = false;
            return false;
        }
    }
    return true;
}

bool ExilePlayer::prepare(const std::filesystem::path& folder, std::uint32_t meshBase)
{
    loaded_ = false;
    meshBase_ = meshBase;
    if (!cookExile(folder, kit_)) return false;
    animations_.clear();
    if (!animations_.load(kit_.bundles[0])) return false;
    for (std::size_t i = 0; i < kExileClipCount; ++i) {
        const ExileClip clip = static_cast<ExileClip>(i);
        clips_[i] = animations_.findClip(name(clip));
        if (clips_[i] == AnimationSet::kNoClip) {
            logf(LogLevel::Error, "{}: the Exile has no {} clip", folder.string(), name(clip));
            return false;
        }
        glm::vec3 start;
        glm::vec3 end;
        const float duration = animations_.clipDuration(clips_[i]);
        travelSpeed_[i] = 0.0f;
        if (duration > 0.0f && animations_.rootTranslation(clips_[i], 0.0f, start) && animations_.rootTranslation(clips_[i], duration, end)) {
            travelSpeed_[i] = std::hypot(end.x - start.x, end.z - start.z) / duration;
        }
    }
    loaded_ = true;
    return true;
}

bool ExilePlayer::pose(const ExileClipChoice& choice, float locomotionPhase, std::vector<glm::mat4>& skin) const
{
    const std::uint32_t clip = clips_[static_cast<std::size_t>(choice.clip)];
    const float duration = animations_.clipDuration(clip);
    const float phase = choice.locomotion ? locomotionPhase - std::floor(locomotionPhase) : choice.progress;
    glm::vec3 root(0.0f);
    if (!animations_.sample(clip, phase * duration, skin, root)) return false;
    for (glm::mat4& m : skin) {
        m[3].x -= root.x;
        m[3].z -= root.z;
    }
    return true;
}

void ExilePlayer::appendPosed(const std::vector<glm::mat4>& skin, glm::dvec3 feet, float yaw, RenderView& view) const
{
    const float safeYaw = std::isfinite(yaw) ? yaw : 0.0f;
    const glm::mat3 facing = glm::mat3_cast(glm::angleAxis(-safeYaw, glm::vec3(0.0f, 1.0f, 0.0f)));

    RenderInstance body;
    body.mesh = meshBase_;
    body.position = feet;
    body.basis = facing;
    body.jointOffset = static_cast<std::uint32_t>(view.jointPalette.size());
    body.jointCount = static_cast<std::uint32_t>(skin.size());
    view.jointPalette.insert(view.jointPalette.end(), skin.begin(), skin.end());
    view.instances.push_back(body);

    for (const JointSocket& socket : kit_.sockets) {
        if (socket.joint >= skin.size()) continue;
        const glm::mat4 held = skin[socket.joint] * socket.local;
        RenderInstance prop;
        prop.mesh = meshBase_ + socket.mesh;
        prop.position = feet + glm::dvec3(facing * glm::vec3(held[3]));
        prop.basis = facing * glm::mat3(held);
        view.instances.push_back(prop);
    }
}

void ExilePlayer::append(const Scene& scene, double alpha, double frameSeconds, float fixedDt, const Tuning& tuning, RenderView& view)
{
    if (!loaded_) return;
    for (auto [entity, transform, previous, c] : scene.registry().view<const Transform, const PreviousTransform, const Combatant>().each()) {
        if (c.side != 0) continue;
        const glm::dvec3 feet = glm::mix(previous.position, transform.position, alpha);
        const glm::dvec3 step = transform.position - previous.position;
        const float speed = fixedDt > 0.0f ? static_cast<float>(std::hypot(step.x, step.z)) / fixedDt : 0.0f;
        const ExileClipChoice choice = chooseExileClip(c, tuning, speed, fixedDt);
        if (choice.locomotion && std::isfinite(frameSeconds)) {
            const std::size_t index = static_cast<std::size_t>(choice.clip);
            const float duration = animations_.clipDuration(clips_[index]);
            const float rate = choice.clip == ExileClip::Idle ? 1.0f : exileLocomotionRate(speed, travelSpeed_[index]);
            if (duration > 0.0f) {
                locomotionPhase_ += static_cast<float>(frameSeconds) * rate / duration;
                locomotionPhase_ -= std::floor(locomotionPhase_);
            }
        }
        std::vector<glm::mat4> skin;
        if (pose(choice, locomotionPhase_, skin)) appendPosed(skin, feet, c.yaw, view);
    }
}

}
