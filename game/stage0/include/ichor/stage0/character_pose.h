#pragma once

#include "ichor/stage0/combat.h"

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

namespace ichor::stage0 {

constexpr float kBodyRadius = 0.3f; // the capsule mesh and the lying pose agree on this

// Render-only: nothing here touches hashed state. The local frame is x right, y up, forward -Z,
// origin at the character's feet, matching yaw 0 in the combat code.
struct PoseInput {
    CombatState state = CombatState::Idle;
    AttackKind attack = AttackKind::Light;
    float stateTime = 0.0f;
    float windup = 0.35f;
    float active = 0.15f;
    float recovery = 0.4f;
    float dodgeDuration = 0.35f;
    bool blocking = false;
};

struct PartPose {
    glm::vec3 position{0.0f}; // grip point or shield centre
    glm::vec3 axis{0.0f, 1.0f, 0.0f}; // spear shaft direction, or shield face normal
};

struct CharacterPose {
    float lean = 0.0f; // radians, positive tips the torso towards forward
    float fall = 0.0f; // 0 standing to 1 lying
    float crouch = 0.0f; // metres the body drops
    PartPose spear;
    PartPose shield;
};

CharacterPose characterPose(const PoseInput& input);
PoseInput poseInputFor(const Combatant& combatant, const Tuning& tuning);

struct PartTransform {
    glm::dvec3 position{0.0};
    glm::mat3 basis{1.0f};
};

struct CharacterTransforms {
    PartTransform body;
    PartTransform spear;
    PartTransform shield;
};

// The body mesh stands on its foot with +Y up, the spear mesh runs along +Y from its grip, the
// shield mesh is a disc whose face normal is +Y.
CharacterTransforms characterTransforms(const CharacterPose& pose, glm::dvec3 feet, float yaw);

// Basis that carries +Y onto `axis`. A zero or non-finite axis leaves +Y alone.
glm::mat3 basisAlong(glm::vec3 axis);

}
