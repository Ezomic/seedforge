#pragma once

#include "ichor/world/forged_biome.h"
#include "ichor/world/heightfield.h"
#include "ichor/world/vegetation.h"

#include <worldforge/biome_map.h>

#include <functional>
#include <vector>

namespace ichor {

// Per-biome scenery for the forged world (SEED-560): which species stand where. The tree scatter of
// scatterBiomeVegetation decides how many trees a cell holds; this decides what each one is by the biome it
// stands in, and adds the ground cover (grass, bushes, dry shrubs, cacti, reeds), dead trees and stones.
//
// The chances are per candidate: ground cover candidates sit on a jittered grid of kCoverSpacingM, dead trees on
// one of kDeadTreeSpacingM. The cover chances of a biome sum to at most 1, so each candidate grows at most one thing.
struct BiomeScenery {
    float grass;
    float bush;
    float shrub;  // dry shrub
    float cactus;
    float reed;   // only on a shore
    float stone;
    float deadTree;
    float coniferShare; // of the living trees the tree scatter plants
    float deadShare;    // of the trees the tree scatter plants, turned into bare trunks
};

const BiomeScenery& biomeScenery(worldforge::BiomeId biome);

constexpr float kCoverSpacingM = 4.5f;
constexpr float kDeadTreeSpacingM = 16.0f;
constexpr float kReedShoreM = 3.5f;

// Where nothing may stand. `blocked` rules out a position for everything (a road, a building, open water);
// `wet` is where water lies, which reeds stand next to. Either may be empty: nothing blocked, and no reeds.
struct SceneryGuard {
    std::function<bool(double x, double z)> blocked;
    std::function<bool(double x, double z)> wet;
};

// Plants a cell: scatterBiomeVegetation's trees and rocks, each tree given its species by the biome it stands
// in and dropped where the guard blocks it, then the ground cover. Depends only on the world seed, the cell, the
// biome map, the guard and the parameters, never on the order cells are asked for in.
void scatterForgedScenery(const Heightfield& heightfield, const ForgedBiomeField& biomes, const SceneryGuard& guard, CellId id, const VegetationParams& base,
    std::vector<VegetationInstance>& out);

}
