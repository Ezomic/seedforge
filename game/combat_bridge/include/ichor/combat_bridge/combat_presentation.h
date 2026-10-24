#pragma once

#include "ichor/core/overlay_mode.h"
#include "ichor/sim/physics.h"
#include "ichor/stage0/combat.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace ichor {

// What the forged combat view draws on top of the bodies (SEED-656). Presentation only: every function here reads
// combat state and writes lines, and nothing in it reaches the simulation, a hash or the replay corpus.

enum class TelegraphPhase : std::uint8_t { None, Windup, Active, Recovery };

// The enemy's attack as the eye should read it, a pure function of its combat state and the tuning's timings.
struct EnemyTelegraph {
    TelegraphPhase phase = TelegraphPhase::None;
    bool heavy = false;
    float progress = 0.0f; // Windup: charge 0..1. Active: 1. Recovery: what is left of it, 1..0.
};

EnemyTelegraph enemyTelegraph(const stage0::Combatant& enemy, const stage0::Tuning& tuning);

// The Stage 0 debug aids (spacing rings, wind-up and heading lines, collider lines) stay off in the forged combat
// view. F3 in its full mode, or --combat-debug, brings them all back.
struct CombatDebugAids {
    bool stageLines = false;
    bool colliders = false;
};

CombatDebugAids combatDebugAids(OverlayMode overlay, bool forced);

// A ground cone from the enemy's feet out to where its blow lands, filled up to the charge during the wind-up, solid
// while the blow is live, shrinking through the recovery. `groundHeight` gives the terrain's y at an x and z, so the
// cone lies on the ground instead of cutting through it.
void appendEnemyTelegraph(const EnemyTelegraph& telegraph, glm::dvec3 feet, glm::vec3 forward, const stage0::Tuning& tuning,
    const std::function<double(double, double)>& groundHeight, std::vector<LineSegment>& out);

}
