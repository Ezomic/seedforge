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

// One cell's navmesh tile as Detour serialises it, built off the game thread
// from the heightfield alone. The tile covers its cell and was rasterised
// with a border into the neighbours, so adjoining tiles meet at the seam.
struct NavTile {
    std::vector<unsigned char> data;
    bool empty() const { return data.empty(); }
};

// Returns false on a build failure. A cell with nothing to walk on returns
// true with an empty tile.
bool buildNavTile(const Heightfield& heightfield, CellId id, const NavParams& params, NavTile& out);

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
