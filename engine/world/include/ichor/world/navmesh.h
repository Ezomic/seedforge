#pragma once

#include "ichor/world/heightfield.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <vector>

class dtNavMesh;
class dtNavMeshQuery;

namespace ichor {

struct NavParams {
    float cellSize = 1.0f;   // voxel width, metres
    float cellHeight = 0.5f; // voxel height, metres
    float agentHeight = 1.8f;
    float agentRadius = 0.5f;
    float agentClimb = 0.6f;
    float maxSlopeDegrees = 45.0f;
};

// Detour area ids a surface can hand out. Ground is Recast's walkable area; a lower id is cheaper to
// path over once NavWorld::findPath weighs it.
constexpr unsigned char kNavAreaGround = 63;
constexpr unsigned char kNavAreaFord = 61;
constexpr unsigned char kNavAreaRoad = 62;
constexpr unsigned char kNavAreaBlocked = 0;

// Which ground an agent may stand on, asked once per voxel column of a tile after the heightfield is
// voxelised. Return kNavAreaBlocked for ground that is not walkable, or one of the walkable areas.
class NavSurface {
public:
    virtual ~NavSurface() = default;
    virtual unsigned char areaAt(double x, double z) const = 0;
};

// One cell's navmesh tile as Detour serialises it, built off the game thread
// from the heightfield alone. The tile covers its cell and was rasterised
// with a border into the neighbours, so adjoining tiles meet at the seam.
struct NavTile {
    std::vector<unsigned char> data;
    bool empty() const { return data.empty(); }
};

// Returns false on a build failure. A cell with nothing to walk on returns
// true with an empty tile.
// A surface, when given, can rule out voxels (water) and mark others as road or ford.
bool buildNavTile(const Heightfield& heightfield, CellId id, const NavParams& params, NavTile& out, const NavSurface* surface = nullptr);

// The tiles of the resident cells, linked to their neighbours as they arrive.
class NavWorld {
public:
    NavWorld();
    ~NavWorld();
    NavWorld(const NavWorld&) = delete;
    NavWorld& operator=(const NavWorld&) = delete;

    bool init(const WorldDesc& desc, std::uint32_t maxTiles);
    bool addTile(CellId id, NavTile&& tile);
    void removeTile(CellId id);
    std::size_t tileCount() const { return tiles_; }

    // A straight-line path over the tiles that are loaded; false when either
    // end is off the mesh or the tiles between them are not resident.
    bool findPath(glm::dvec3 from, glm::dvec3 to, std::vector<glm::dvec3>& path) const;

private:
    struct Deleter {
        void operator()(dtNavMesh* mesh) const;
    };
    struct QueryDeleter {
        void operator()(dtNavMeshQuery* query) const;
    };
    std::unique_ptr<dtNavMesh, Deleter> mesh_;
    std::unique_ptr<dtNavMeshQuery, QueryDeleter> query_;
    std::size_t tiles_ = 0;
};

}
