#pragma once

#include "ichor/world/heightfield.h"
#include "ichor/world/vegetation.h"

#include <glm/vec3.hpp>

#include <worldforge/biome_map.h>
#include <worldforge/forge_world.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// The bake step that drives the terrain splat layers and the vegetation scatter from the forged biome
// map (doc 08 phase 12) instead of global constants. It reads worldforge::BiomeMap in one place, here.
//
// What a biome decides is a BiomeSurface: how thick the forest is, how often a rock shows, where the
// snow line lies, the ground colour, and the grass/rock/snow mix of the terrain splat layers
// (forgeTerrainMaterials). The table is fixed data, so the same biome always gives the same surface.
struct BiomeSurface {
    float forestDensity;    // VegetationParams::forestDensity: 0 is bare ground, 1 the full forest pattern
    float rockChance;       // VegetationParams::rockChance on level ground
    float snowLineM;        // metres above sea level; trees stop here (at or below 0 the biome grows none)
    glm::vec3 groundColour; // linear colour of bare ground
    glm::vec3 splat;        // weights of the grass, rock and snow splat layers; they sum to 1
};

const BiomeSurface& biomeSurface(worldforge::BiomeId biome);

// The forged biomes laid over the engine's world. Cells are looked up by world position in metres,
// the first and last forged cell sitting on the world's edges like the forged heights do.
class ForgedBiomeField {
public:
    ForgedBiomeField() = default;
    // An inconsistent map (not square, mismatched arrays) gives an empty field.
    ForgedBiomeField(const worldforge::BiomeMap& map, std::uint32_t sizeMetres);

    bool empty() const { return width_ == 0; }

    // The biome of the forged cell nearest to a world position; positions off the world clamp to its edge.
    worldforge::BiomeId biomeAt(double x, double z) const;

    // The biome that holds most of a streaming cell's land (water only when the cell has no land), ties
    // to the lowest id. It depends on the cell alone, never on which cells were asked for before.
    worldforge::BiomeId cellBiome(const WorldDesc& desc, CellId id) const;

    // Splat weights and ground colour at a position: the nearest cell's biome blended with its secondary
    // biome by the map's blend byte, so the terrain fades across a border instead of cutting at it.
    glm::vec3 splatAt(double x, double z) const;
    glm::vec3 groundColourAt(double x, double z) const;

private:
    int width_ = 0;
    std::uint32_t sizeMetres_ = 0;
    std::vector<std::uint8_t> biome_;
    std::vector<std::uint8_t> secondary_;
    std::vector<std::uint8_t> blend_;

    int forgedCoord(double metres) const;
    std::size_t indexAt(double x, double z) const;
};

struct ForgedBiomes {
    ForgedBiomeField field;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }
};

// Needs the world to have completed phase 5 (biomes) and a valid desc.
ForgedBiomes biomesFromForgedWorld(const worldforge::ForgedWorld& world, const WorldDesc& desc);

// `base` with the biome's forest density, rock chance and snow line. VegetationParams heights are
// measured from desc.minHeight, so the sea sits at desc.waterHeight (a dry desc puts it at the floor);
// base.minHeight is added above the sea, and the spacings and slopes stay as they were.
VegetationParams vegetationParamsFor(worldforge::BiomeId biome, const WorldDesc& desc, const VegetationParams& base = {});

// scatterVegetation for one cell with the parameters of the biome that cell is in.
void scatterBiomeVegetation(const Heightfield& heightfield, const ForgedBiomeField& biomes, CellId id, const VegetationParams& base, std::vector<VegetationInstance>& out);

}
