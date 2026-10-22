#pragma once

#include "ichor/world/heightfield.h"
#include "ichor/world/hydrology.h"

#include <glm/vec2.hpp>

#include <vector>

namespace ichor {

// How close each ground sample of a cell lies to water, for the forged terrain's look: x is sand (a beach
// beside a lake or the sea, low above the water), y is wet soil (the bank of a river). Both run from 0 (dry,
// far from water) to 1. One entry per sample of the cell, in the cell's row order; an empty vector when the
// hydrology is empty or of another size. Distances come from a chamfer transform over the cell and a margin
// around it, in plain float arithmetic, so the same cell gives the same answer on every machine.
std::vector<glm::vec2> terrainShore(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, CellId id);

}
