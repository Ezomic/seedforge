#include "ichor/stage0/character_pose.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

namespace {

float clamp01(float v)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

float smooth(float t)
{
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

float progress(float time, float duration)
{
    return duration > 1e-6f && std::isfinite(duration) ? clamp01(time / duration) : 1.0f;
}

PartPose mix(const PartPose& a, const PartPose& b, float t)
{
    return {glm::mix(a.position, b.position, t), glm::normalize(glm::mix(a.axis, b.axis, t))};
}

struct Rig {
    float lean;
    PartPose spear;
    PartPose shield;
};

Rig mix(const Rig& a, const Rig& b, float t)
{
    return {glm::mix(a.lean, b.lean, t), mix(a.spear, b.spear, t), mix(a.shield, b.shield, t)};
}

// Spear tip sits 1.4 m past the grip, so a level thrust from z = -0.3 reaches about attackRange.
const Rig kIdle{0.0f, {{0.35f, 1.0f, -0.15f}, glm::normalize(glm::vec3(0.0f, 0.55f, -0.85f))}, {{-0.4f, 1.0f, -0.2f}, glm::normalize(glm::vec3(-0.3f, 0.0f, -1.0f))}};
const Rig kGuard{0.05f, {{0.35f, 0.8f, 0.0f}, glm::normalize(glm::vec3(0.0f, 0.3f, -1.0f))}, {{-0.05f, 1.1f, -0.5f}, glm::vec3(0.0f, 0.05f, -1.0f)}};

// Light: draw the spear back to the hip, level, then drive it straight out.
const Rig kLightCocked{-0.15f, {{0.3f, 1.0f, 0.4f}, glm::normalize(glm::vec3(0.05f, 0.1f, -1.0f))}, kIdle.shield};
const Rig kLightThrust{0.2f, {{0.3f, 1.0f, -0.3f}, glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f))}, kIdle.shield};

// Heavy: lift the spear overhead and lean back, then bring it down in an arc.
const Rig kHeavyRaised{-0.4f, {{0.25f, 1.7f, 0.1f}, glm::normalize(glm::vec3(0.1f, 0.9f, -0.4f))}, kIdle.shield};
const Rig kHeavySlam{0.45f, {{0.1f, 1.2f, -0.4f}, glm::normalize(glm::vec3(0.0f, -0.35f, -0.94f))}, kIdle.shield};

const Rig kStaggered{-0.5f, {{0.4f, 0.7f, 0.1f}, glm::normalize(glm::vec3(0.3f, -0.4f, -0.6f))}, {{-0.5f, 0.7f, 0.0f}, glm::normalize(glm::vec3(-0.6f, 0.0f, -0.8f))}};
const Rig kDodge{0.3f, {{0.3f, 0.8f, 0.2f}, glm::normalize(glm::vec3(0.0f, 0.2f, -1.0f))}, {{-0.35f, 0.8f, -0.1f}, glm::normalize(glm::vec3(-0.4f, 0.0f, -1.0f))}};

const PartPose kDroppedSpear{{0.6f, 0.1f, -0.3f}, glm::normalize(glm::vec3(1.0f, 0.0f, 0.2f))};
const PartPose kDroppedShield{{-0.6f, 0.05f, 0.2f}, {0.0f, 1.0f, 0.0f}};

constexpr float kFallSeconds = 0.4f;
constexpr float kPi = 3.14159265358979323846f;

}

CharacterPose characterPose(const PoseInput& input)
{
    const bool heavy = input.attack == AttackKind::Heavy;
    const Rig& cocked = heavy ? kHeavyRaised : kLightCocked;
    const Rig& struck = heavy ? kHeavySlam : kLightThrust;
    const Rig rest = input.blocking ? kGuard : kIdle;
    const float time = std::isfinite(input.stateTime) ? std::max(0.0f, input.stateTime) : 0.0f;

    Rig rig = rest;
    CharacterPose out;
    switch (input.state) {
    case CombatState::Windup:
        rig = mix(kIdle, cocked, smooth(progress(time, input.windup)));
        break;
    case CombatState::Active:
        // The blow is fast and ends hard: ease out so the strike reads as a snap.
        rig = mix(cocked, struck, 1.0f - (1.0f - progress(time, input.active)) * (1.0f - progress(time, input.active)));
        break;
    case CombatState::Recovery:
        rig = mix(struck, kIdle, smooth(progress(time, input.recovery)));
        break;
    case CombatState::Dodging: {
        const float t = std::sin(progress(time, input.dodgeDuration) * kPi);
        rig = mix(kIdle, kDodge, t);
        out.crouch = 0.25f * t;
        break;
    }
    case CombatState::Staggered:
        rig = mix(kIdle, kStaggered, smooth(progress(time, 0.15f)));
        break;
    case CombatState::Dead: {
        const float fall = smooth(progress(time, kFallSeconds));
        rig = kStaggered;
        rig.spear = mix(kStaggered.spear, kDroppedSpear, fall);
        rig.shield = mix(kStaggered.shield, kDroppedShield, fall);
        out.fall = fall;
        break;
    }
    case CombatState::Idle:
        break;
    }
    out.lean = rig.lean;
    out.spear = rig.spear;
    out.shield = rig.shield;
    return out;
}

PoseInput poseInputFor(const Combatant& c, const Tuning& tuning)
{
    const bool heavy = c.attack == static_cast<std::uint8_t>(AttackKind::Heavy);
    PoseInput in;
    in.state = c.combatState();
    in.attack = heavy ? AttackKind::Heavy : AttackKind::Light;
    in.stateTime = c.stateTime;
    in.windup = heavy ? tuning.heavyWindup : tuning.lightWindup;
    in.active = heavy ? tuning.heavyActive : tuning.lightActive;
    in.recovery = heavy ? tuning.heavyRecovery : tuning.lightRecovery;
    in.dodgeDuration = tuning.dodgeDuration;
    in.blocking = c.blocking;
    return in;
}

glm::mat3 basisAlong(glm::vec3 axis)
{
    const float len2 = glm::dot(axis, axis);
    if (!std::isfinite(len2) || len2 < 1e-12f) {
        return glm::mat3(1.0f);
    }
    const glm::vec3 to = axis / std::sqrt(len2);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (to.y < -0.9999f) {
        return glm::mat3_cast(glm::angleAxis(kPi, glm::vec3(1.0f, 0.0f, 0.0f)));
    }
    return glm::mat3_cast(glm::rotation(up, to));
}

CharacterTransforms characterTransforms(const CharacterPose& pose, glm::dvec3 feet, float yaw)
{
    const float safeYaw = std::isfinite(yaw) ? yaw : 0.0f;
    const glm::mat3 facing = glm::mat3_cast(glm::angleAxis(-safeYaw, glm::vec3(0.0f, 1.0f, 0.0f)));
    // Lean tips the torso towards -Z (forward); a fallen body tips back, about the feet.
    const float angle = -(pose.lean * (1.0f - pose.fall) - pose.fall * kPi * 0.5f);
    const glm::mat3 tip = glm::mat3_cast(glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f)));
    const glm::vec3 pivot(0.0f, 0.9f * (1.0f - pose.fall), 0.0f);
    const glm::vec3 drop(0.0f, -pose.crouch, 0.0f);

    const auto place = [&](glm::vec3 local, const glm::mat3& basis) {
        const glm::vec3 moved = pivot + tip * (local - pivot) + drop;
        return PartTransform{feet + glm::dvec3(facing * moved), facing * tip * basis};
    };
    const auto placeFixed = [&](const PartPose& part) {
        const glm::vec3 moved = pivot + tip * (part.position - pivot) + drop;
        return PartTransform{feet + glm::dvec3(facing * moved), facing * (tip * basisAlong(part.axis))};
    };
    CharacterTransforms out;
    out.body = place(glm::vec3(0.0f), glm::mat3(1.0f));
    out.spear = placeFixed(pose.spear);
    out.shield = placeFixed(pose.shield);
    return out;
}

}
