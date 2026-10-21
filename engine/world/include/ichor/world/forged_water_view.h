#pragma once

#include "ichor/world/forged_game.h"

namespace ichor {

enum class WaterViewKind { River, Lake };

// Moves the spawn to dry ground on the bank of the forged world's biggest river, or of its biggest lake, and
// aims the view at the water, so a screenshot of the water needs no hunting for it. Deterministic. Returns false,
// leaving the spawn alone, when the world has no such water or no dry bank beside it.
bool spawnAtWater(ForgedGame& game, WaterViewKind kind);

}
