#pragma once

#include "ichor/traversal/roads.h"
#include "ichor/world/navmesh.h"
#include "ichor/world/water_field.h"

namespace ichor {

// The bake of the navmesh from the forged world (doc 08 phase 12): the tile build of navmesh.h with the
// forged water and roads deciding what can be walked. Open water is not walkable. Shallow water, a
// ford, is, at a higher path cost; a road is walkable even over water (a bridge) and the cheapest
// ground, so paths follow roads where they go the same way.
struct ForgedNavParams {
    float fordDepth = 0.5f;     // metres of water an agent still wades; deeper is blocked
    float roadCoverage = 0.5f;  // road coverage from which ground counts as road
};

class ForgedNavSurface final : public NavSurface {
public:
    // The water field and road field (null for none) must outlive the surface.
    ForgedNavSurface(const WaterField& water, const RoadField* roads, const ForgedNavParams& params = {});

    unsigned char areaAt(double x, double z) const override;

private:
    const WaterField& water_;
    const RoadField* roads_;
    ForgedNavParams params_;
};

// buildNavTile over the forged heightfield with its water and roads respected.
bool buildForgedNavTile(const Heightfield& heightfield, const WaterField& water, const RoadField* roads, CellId id, const NavParams& nav, NavTile& out,
    const ForgedNavParams& params = {});

}
