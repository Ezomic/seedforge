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
class WaterField {
public:
    struct Sample {
        float depth = 0.0f;   // metres of water over the ground; zero on dry land
        float surface = 0.0f; // height of the water's surface; the ground's height when dry
        bool wet() const { return depth > 0.0f; }
    };

    // Keeps the heightfield alive by reference: it must outlive the field.
    WaterField(const Heightfield& heightfield, std::shared_ptr<const Hydrology> hydrology, const HydrologyParams& rivers, const WaterMeshParams& mesh = {});

    // Depth blends smoothly between the four samples around the point, so the shore is a slope and
    // not a step. Outside the map it is dry.
    Sample at(double x, double z) const;

    const Hydrology& hydrology() const { return *hydrology_; }

private:
    float depthAtSample(std::uint32_t sx, std::uint32_t sz) const;

    const Heightfield& heightfield_;
    std::shared_ptr<const Hydrology> hydrology_;
    HydrologyParams rivers_;
    WaterMeshParams mesh_;
};

}
