#pragma once

#include "ichor/sim/scene.h"
#include "ichor/world/forged_game.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace ichor {

// The five terrain conditions of doc 18 that every enemy is validated in, found in a forged world (SEED-661). The
// stage0 terrain harness runs the same names on a flat plane of discs; these are the real ground.
enum class FieldCondition : std::uint8_t { OpenField, DenseForest, NarrowDefile, SteepSlope, ShallowWater };
constexpr std::size_t kFieldConditionCount = 5;
const char* name(FieldCondition condition);

// One place to put a fight: the foe on its post and the player a few metres away, both on ground the walking rules
// let them stand on and the whole way between them walkable.
struct TerrainTrial {
    FieldCondition condition = FieldCondition::OpenField;
    bool foeHigher = false; // slope and water: which of the two stands on the higher ground
    glm::dvec3 foe{0.0};
    glm::dvec3 player{0.0};
};

struct TrialSearch {
    double separationM = 10.0; // foe to player
};

// The first place, in a fixed scan order, that holds the condition. Pure in the game and the search, so one seed gives
// one answer on every machine. Not found when the world has no such ground (a defile is rare).
//   OpenField     level, dry and clear of forest for 12 m around both fighters.
//   DenseForest   dry, gentle ground in the thick of a forest patch (the tree scatter's own density pattern).
//   NarrowDefile  a walkable floor with ground too steep to walk on both sides of the whole line between them.
//   SteepSlope    ground at the top of what is walked, rising from one fighter to the other.
//   ShallowWater  a dry bank on one side and wading depth on the other.
// Trees are not solid in the engine yet, so the forest is the density of the ground cover and nothing stops a step.
std::optional<TerrainTrial> findTrial(const ForgedGame& game, FieldCondition condition, bool foeHigher = false, const TrialSearch& search = {});

// The walking rules the app puts on every body in a forged world: ground too steep or water too deep stops it, and
// wading slows it. Install on a scene with installFieldRules.
void installFieldRules(Scene& scene, const ForgedGame& game);

}
