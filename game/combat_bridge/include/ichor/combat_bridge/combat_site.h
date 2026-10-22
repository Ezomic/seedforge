#pragma once

#include "ichor/world/forged_game.h"

#include <glm/vec3.hpp>

#include <cstddef>

namespace ichor {

// Where the proof-of-concept fight happens in a forged world: one enemy standing just outside the edge of a
// settlement. A pure function of the ForgedGame, so one seed and grid always give the same site.
struct CombatSite {
    bool found = false;
    std::size_t settlement = 0; // the settlement the enemy guards, an index into ForgedGame::sites
    glm::dvec3 enemyPost{0.0};  // the enemy's feet
    glm::dvec3 duelStart{0.0};  // dry ground a few metres from the post, on the settlement side: where --combat-start enemy begins
};

// The site beside the settlement nearest the spawn. Not found when the game has no settlements or no clear ground near one.
CombatSite findCombatSite(const ForgedGame& game);

// Where the player stands again after dying, once they last were in settlement `index` (which must be a valid index).
glm::dvec3 respawnPoint(const ForgedGame& game, std::size_t index);

// The settlement whose area (by its tier's radius, plus some slack) holds the point, the nearest centre first; false when it is in the wild.
bool settlementAt(const ForgedGame& game, double x, double z, std::size_t& index);

}
