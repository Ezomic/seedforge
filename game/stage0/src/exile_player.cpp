#include "ichor/stage0/exile_player.h"

#include "ichor/anim/exile_avatar.h"
#include "ichor/core/log.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace ichor::stage0 {

namespace {

constexpr float kPiTurn = 3.14159265f; // a step this big always reaches the target, so the turn is a snap wrapped to (-pi, pi]

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
        out.progress = fraction(static_cast<float>(std::max(c.blockTicks, 0)) * fixedDt, kExileGuardRaiseSeconds) * kExileGuardPhase;
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
    return std::clamp(speed / clipTravelSpeed, kExileMinRate, kExileMaxRate);
}

void blendSkin(const std::vector<glm::mat4>& from, const std::vector<glm::mat4>& to, float t, std::vector<glm::mat4>& out)
{
    const auto split = [](const glm::mat4& m, glm::vec3& scale, glm::quat& rotation) {
        glm::mat3 basis(m);
        scale = {glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2])};
        for (int k = 0; k < 3; ++k) {
            if (scale[k] > 1e-6f) basis[k] /= scale[k];
        }
        rotation = glm::quat_cast(basis);
    };
    const float w = std::clamp(t, 0.0f, 1.0f);
    out.resize(to.size());
    for (std::size_t i = 0; i < to.size(); ++i) {
        if (i >= from.size()) {
            out[i] = to[i];
            continue;
        }
        glm::vec3 scaleA;
        glm::vec3 scaleB;
        glm::quat rotationA;
        glm::quat rotationB;
        split(from[i], scaleA, rotationA);
        split(to[i], scaleB, rotationB);
        const glm::mat3 rotation = glm::mat3_cast(glm::slerp(rotationA, rotationB, w));
        const glm::vec3 scale = glm::mix(scaleA, scaleB, w);
        glm::mat4 m(1.0f);
        for (int k = 0; k < 3; ++k) m[k] = glm::vec4(rotation[k] * scale[k], 0.0f);
        m[3] = glm::vec4(glm::mix(glm::vec3(from[i][3]), glm::vec3(to[i][3]), w), 1.0f);
        out[i] = m;
    }
}

bool buildExileRunRig(const std::vector<std::string>& jointNames, ExileRunRig& rig)
{
    rig = ExileRunRig{};
    const auto find = [&](std::string_view name, std::uint32_t& out) {
        const auto it = std::find(jointNames.begin(), jointNames.end(), std::string("mixamorig:") + std::string(name));
        if (it == jointNames.end()) return false;
        out = static_cast<std::uint32_t>(it - jointNames.begin());
        return true;
    };
    if (!find("Hips", rig.hips) || !find("Spine2", rig.chest) || !find("LeftArm", rig.leftArm) || !find("RightArm", rig.rightArm)) return false;
    for (std::uint32_t j = 0; j < jointNames.size(); ++j) {
        const std::string& n = jointNames[j];
        const auto has = [&](const char* part) { return n.find(part) != std::string::npos; };
        if (!(has("Spine") || has("Shoulder") || has("Arm") || has("Hand") || has("Neck") || has("Head"))) continue;
        rig.upperBody.push_back(j);
        if (!has("Arm") && !has("Hand")) continue;
        (has("mixamorig:Left") ? rig.leftArmJoints : rig.rightArmJoints).push_back(j);
    }
    return !rig.leftArmJoints.empty() && !rig.rightArmJoints.empty();
}

void correctRunPose(const ExileRunRig& rig, const std::vector<glm::mat4>& world, std::vector<glm::mat4>& skin)
{
    if (world.size() != skin.size() || rig.hips >= world.size() || rig.chest >= world.size() || rig.leftArm >= world.size() || rig.rightArm >= world.size()) return;
    const glm::vec3 hips(world[rig.hips][3]);
    const glm::vec3 spine = glm::vec3(world[rig.chest][3]) - hips;
    const float lean = std::atan2(-spine.z, spine.y);
    const float back = std::max(lean - kExileRunLean, 0.0f);
    const auto about = [](glm::vec3 pivot, glm::vec3 axis, float angle) {
        return glm::translate(glm::mat4(1.0f), pivot) * glm::rotate(glm::mat4(1.0f), angle, axis) * glm::translate(glm::mat4(1.0f), -pivot);
    };
    const glm::mat4 sitUp = about(hips, glm::vec3(1.0f, 0.0f, 0.0f), back);
    for (const std::uint32_t j : rig.upperBody) skin[j] = sitUp * skin[j];
    const glm::vec3 left = glm::vec3(sitUp * world[rig.leftArm][3]);
    const glm::vec3 right = glm::vec3(sitUp * world[rig.rightArm][3]);
    const glm::mat4 leftLevel = about(left, glm::vec3(1.0f, 0.0f, 0.0f), -back);
    const glm::mat4 rightLevel = about(right, glm::vec3(1.0f, 0.0f, 0.0f), -back);
    for (const std::uint32_t j : rig.leftArmJoints) skin[j] = leftLevel * skin[j];
    for (const std::uint32_t j : rig.rightArmJoints) skin[j] = rightLevel * skin[j];
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
    const std::uint32_t dodge = clips_[static_cast<std::size_t>(ExileClip::Dodge)];
    glm::vec3 dodgeStart;
    glm::vec3 dodgeEnd;
    dodgeTravelYaw_ = 0.0f;
    if (animations_.rootTranslation(dodge, 0.0f, dodgeStart) && animations_.rootTranslation(dodge, animations_.clipDuration(dodge), dodgeEnd) &&
        std::hypot(dodgeEnd.x - dodgeStart.x, dodgeEnd.z - dodgeStart.z) > 0.05f) {
        dodgeTravelYaw_ = yawOfMotion(dodgeEnd.x - dodgeStart.x, dodgeEnd.z - dodgeStart.z);
    }
    bodyYaw_ = 0.0f;
    hasBodyYaw_ = false;
    locomotionPhase_ = 0.0f;
    lastClip_ = ExileClip::Idle;
    lastSkin_.clear();
    fadeFrom_.clear();
    fadeSeconds_ = kExileFadeSeconds;
    hasRunRig_ = buildExileRunRig(animations_.jointNames(animations_.clipSkeleton(clips_[static_cast<std::size_t>(ExileClip::Run)])), runRig_);
    if (!hasRunRig_) log(LogLevel::Warn, "the Exile's skeleton lacks the joints to straighten the Run clip");
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
    if (choice.clip == ExileClip::Run && hasRunRig_) {
        std::vector<glm::mat4> world;
        if (animations_.samplePose(clip, phase * duration, world)) correctRunPose(runRig_, world, skin);
    }
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
        const glm::mat4 held = heldMatrix(kit_, socket, skin);
        RenderInstance prop;
        prop.mesh = meshBase_ + socket.mesh;
        prop.position = feet + glm::dvec3(facing * glm::vec3(held[3]));
        prop.basis = facing * glm::mat3(held);
        view.instances.push_back(prop);
    }
}

float ExilePlayer::bodyYaw(const Combatant& c, const ExileClipChoice& choice, glm::dvec3 step, float speed, float seconds)
{
    if (!hasBodyYaw_ || !std::isfinite(bodyYaw_)) {
        bodyYaw_ = std::isfinite(c.yaw) ? c.yaw : 0.0f;
        hasBodyYaw_ = true;
    }
    if (choice.clip == ExileClip::Dodge && glm::dot(c.dodgeDirection, c.dodgeDirection) > 1e-6f) {
        // The clip only steps one way, so the body is turned until that step lies along the dodge.
        bodyYaw_ = turnToward(bodyYaw_, yawOfMotion(c.dodgeDirection.x, c.dodgeDirection.z) - dodgeTravelYaw_, 2.0f * kPiTurn);
        return bodyYaw_;
    }
    // The clips only run forwards: a body that walks backwards or sideways while facing the aim would moonwalk.
    const bool travelling = choice.locomotion && speed >= kExileStandBelow;
    const float wanted = travelling ? yawOfMotion(step.x, step.z) : c.yaw;
    bodyYaw_ = turnToward(bodyYaw_, wanted, (travelling ? kExileTurnPerSecond : kExileActionTurnPerSecond) * seconds);
    return bodyYaw_;
}

void ExilePlayer::append(const Scene& scene, double alpha, double frameSeconds, float fixedDt, const Tuning& tuning, RenderView& view)
{
    if (!loaded_) return;
    bool seen = false;
    for (auto [entity, transform, previous, c] : scene.registry().view<const Transform, const PreviousTransform, const Combatant>().each()) {
        if (c.side != 0) continue;
        seen = true;
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
        const float seconds = std::isfinite(frameSeconds) ? static_cast<float>(std::max(frameSeconds, 0.0)) : 0.0f;
        std::vector<glm::mat4> skin;
        if (!pose(choice, locomotionPhase_, skin)) continue;
        if (!lastSkin_.empty() && choice.clip != lastClip_) {
            fadeFrom_ = lastSkin_;
            fadeSeconds_ = 0.0f;
        }
        lastClip_ = choice.clip;
        if (fadeSeconds_ < kExileFadeSeconds && !fadeFrom_.empty()) {
            blendSkin(fadeFrom_, skin, fadeSeconds_ / kExileFadeSeconds, skin);
            fadeSeconds_ += seconds;
        }
        lastSkin_ = skin;
        appendPosed(skin, feet, bodyYaw(c, choice, step, speed, seconds), view);
    }
    if (!seen) {
        lastSkin_.clear();
        hasBodyYaw_ = false;
    }
}

}
