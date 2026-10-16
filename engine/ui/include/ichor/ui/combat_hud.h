#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

// The five elements of the combat HUD (doc 13): health, stamina, the Law resource, a damage
// direction indicator and the current target's stagger tick. There is deliberately no field for
// an enemy health bar or a damage number, so no drawing code can be handed one.

struct CombatHudHit {
    // Bearing from the player to the attacker in world yaw, radians, in the same convention as
    // `facingYaw` (increasing to the right).
    float bearing = 0.0f;
    float ageSeconds = 0.0f;
};

struct CombatHudLawResource {
    std::string id;
    float value = 0.0f;
    float max = 0.0f;
};

struct CombatHudTarget {
    float stagger = 0.0f;
    float staggerMax = 0.0f;
};

struct CombatHudInput {
    float health = 0.0f;
    float maxHealth = 0.0f;
    float stamina = 0.0f;
    float maxStamina = 0.0f;
    float facingYaw = 0.0f;
    std::optional<CombatHudLawResource> lawResource;
    std::vector<CombatHudHit> recentHits;
    std::optional<CombatHudTarget> target;
};

struct CombatHudTuning {
    float lowThreshold = 0.25f;
    float indicatorSeconds = 1.5f;
    std::size_t maxIndicators = 8;
};

struct CombatHudBar {
    float fraction = 0.0f;
    bool low = false;
};

struct CombatHudIndicator {
    // Angle of the hit relative to where the player faces, in (-pi, pi]: 0 ahead, positive to
    // the right, pi behind.
    float angle = 0.0f;
    float opacity = 0.0f;
};

struct CombatHudLawView {
    std::string id;
    float fraction = 0.0f;
    float value = 0.0f;
};

struct CombatHudView {
    CombatHudBar health;
    CombatHudBar stamina;
    std::optional<CombatHudLawView> lawResource;
    // Newest first, at most CombatHudTuning::maxIndicators, fading to nothing over
    // indicatorSeconds.
    std::vector<CombatHudIndicator> indicators;
    // 0..1 fill of the tick beside the reticle; absent when nothing is targeted.
    std::optional<float> targetStagger;
};

// Pure and deterministic. NaN, infinite or negative inputs read as empty; a hit that is not yet
// or no longer visible leaves no indicator. Never throws.
CombatHudView buildCombatHudView(const CombatHudInput& input, const CombatHudTuning& tuning = {});

}
