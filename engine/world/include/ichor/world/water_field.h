#pragma once

#include "ichor/world/heightfield.h"
#include "ichor/world/hydrology.h"
#include "ichor/world/water_mesh.h"

#include <memory>

namespace ichor {

// How the water lies at any point of the world: what the player wades through and what the
// water meshes draw. Built from the same hydrology and the same parameters as the meshes, so the
// water you stand in is the water you see. A pure function of the ground, so the same point has
// the same answer on every machine and in every replay.
// How much of their speed a walker keeps wading through water of this depth: all of it over the
// ankles, falling smoothly to a third of it by chest height. Plain float arithmetic, because the
// simulation reads it.
float wadingSpeedFactor(float depth);

class WaterField {
public:
    struct Sample {
        float depth = 0.0f;   // metres of water over the ground; zero on dry land
        float surface = 0.0f; // height of the water's surface; the ground's height when dry
        bool wet() const { return depth > 0.0f; }
    };

    // Keeps the heightfield alive by reference: it must outlive the field. Give it the same river and
    // mesh parameters as the streamer builds the meshes with, or the water waded through is not the
    // water drawn.
    WaterField(const Heightfield& heightfield, std::shared_ptr<const Hydrology> hydrology, const HydrologyParams& rivers, const WaterMeshParams& mesh = {});

    // The surface is the one the water mesh draws, blended across the cell, and the depth is how far
    // it stands over the ground, so the shore is a slope and not a step. Outside the map, or for a point
    // that is not a number, it is dry.
    Sample at(double x, double z) const;

    const Hydrology& hydrology() const { return *hydrology_; }

private:
    const Heightfield& heightfield_;
    std::shared_ptr<const Hydrology> hydrology_;
    HydrologyParams rivers_;
    WaterMeshParams mesh_;
};

}
