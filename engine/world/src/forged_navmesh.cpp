#include "ichor/world/forged_navmesh.h"

namespace ichor {

ForgedNavSurface::ForgedNavSurface(const WaterField& water, const RoadField* roads, const ForgedNavParams& params)
    : water_(water)
    , roads_(roads)
    , params_(params)
{
}

unsigned char ForgedNavSurface::areaAt(double x, double z) const
{
    if (roads_ != nullptr && roads_->coverageAt(x, z) >= params_.roadCoverage) {
        return kNavAreaRoad;
    }
    const float depth = water_.at(x, z).depth;
    if (!(depth <= params_.fordDepth)) {
        return kNavAreaBlocked;
    }
    return depth > 0.0f ? kNavAreaFord : kNavAreaGround;
}

bool buildForgedNavTile(const Heightfield& heightfield, const WaterField& water, const RoadField* roads, CellId id, const NavParams& nav, NavTile& out,
    const ForgedNavParams& params)
{
    const ForgedNavSurface surface(water, roads, params);
    return buildNavTile(heightfield, id, nav, out, &surface);
}

}
