#pragma once

#include "ichor/assets/bundle_types.h"
#include "ichor/traversal/roads.h"
#include "ichor/world/heightfield.h"

#include <worldforge/forge_world.h>

#include <cstdint>
#include <vector>

namespace ichor {

// The bake of the forged road network (doc 08 phase 12, after the heightfield adapter): where the roads
// lie in world metres, as a road query for traversal and as a ribbon mesh per streamed cell. The
// terrain deformation itself is done by libworldforge (RoadMap::heightM); heightfieldFromForgedRoads in
// forged_heightfield.h adapts that terrain, so the ground, the query and the ribbon agree.
//
// A road is a polyline through the centres of the forged cells of RoadEdge::pathCells. Forged cell
// (cx, cy) lies at x = cx * sizeMetres / (grid - 1), z = cy * sizeMetres / (grid - 1), the same
// placement the heightfield adapter uses. Routes that share cells (later roads merge into earlier ones)
// are one segment, belonging to the first edge that laid it.
struct ForgedRoadParams {
    float widthMetres = 4.0f; // the road surface, fully covered
    float shoulderMetres = 2.0f; // beyond the surface on each side, coverage falls to zero
    float lift = 0.1f;        // metres the ribbon lies over the ground, against z-fighting; a piece that would dip under a bump is raised clear of it (raiseOverGround)
};

struct RoadHit {
    float coverage = 0.0f; // 0 off-road to 1 on the surface
    int edge = -1;         // index into RoadMap::edges of the nearest road, -1 when coverage is 0
};

class ForgedRoads final : public RoadField {
public:
    ForgedRoads() = default;

    // Empty (no roads, every query reports none) when the world did not reach the roads phase, has
    // no road edges, or desc or params are invalid or not finite.
    ForgedRoads(const worldforge::ForgedWorld& world, const WorldDesc& desc, const ForgedRoadParams& params = {});

    bool empty() const { return segments_.empty(); }
    std::size_t segmentCount() const { return segments_.size(); }

    RoadHit query(double x, double z) const;
    float coverageAt(double x, double z) const override { return query(x, z).coverage; }

    // Length in metres of one road edge along its polyline, 0 for an index out of range. With the
    // edge from query() this is what rollRoadEncounter takes as its segment.
    double edgeLengthMetres(int edge) const;

    // The ribbon of one cell, positions relative to the cell origin like the terrain's, lying on the
    // heightfield. uv.x runs across the road from 0 to 1, uv.y along it in road widths (so a square texture tile is not stretched). A cell with no
    // road, or a heightfield that is not of this world's desc, comes back with no vertices.
    // Each segment is its own quad, clipped to the cell, so a bend leaves a small wedge on its outer side and
    // the ribbon's ends reach half a width past the cell edge. Segments between cells that are not neighbours
    // are ignored.
    Mesh buildMesh(const Heightfield& heightfield, CellId id) const;

private:
    struct Segment {
        double ax = 0.0;
        double az = 0.0;
        double bx = 0.0;
        double bz = 0.0;
        std::uint32_t edge = 0;
    };

    std::vector<Segment> segments_;
    std::vector<std::vector<std::uint32_t>> buckets_;
    std::vector<double> edgeLength_;
    ForgedRoadParams params_;
    WorldDesc desc_;
    int grid_ = 0;
    double cellSize_ = 0.0;
};

}
