#include "ichor/world/navmesh.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr unsigned short kWalkFlag = 1;

class SilentContext final : public rcContext {
public:
    SilentContext()
    {
        enableLog(false);
        enableTimer(false);
    }
};

template <class T>
struct Owned {
    T* value = nullptr;
    void (*release)(T*) = nullptr;
    ~Owned()
    {
        if (value != nullptr) release(value);
    }
};

}

bool buildNavTile(const Heightfield& heightfield, CellId id, const NavParams& params, NavTile& out)
{
    out.data.clear();
    const WorldDesc& desc = heightfield.desc();
    const float cs = params.cellSize;
    const int walkableHeight = static_cast<int>(std::ceil(params.agentHeight / params.cellHeight));
    const int walkableClimb = static_cast<int>(std::floor(params.agentClimb / params.cellHeight));
    const int walkableRadius = static_cast<int>(std::ceil(params.agentRadius / cs));
    const int border = walkableRadius + 3;
    const int tileSize = static_cast<int>(std::lround(desc.cellMetres / cs));

    const double originX = heightfield.cellOriginX(id);
    const double originZ = heightfield.cellOriginZ(id);
    Heightfield::Cell cell;
    if (!heightfield.cell(id, cell)) {
        return false;
    }

    rcConfig config{};
    config.cs = cs;
    config.ch = params.cellHeight;
    config.walkableSlopeAngle = params.maxSlopeDegrees;
    config.walkableHeight = walkableHeight;
    config.walkableClimb = walkableClimb;
    config.walkableRadius = walkableRadius;
    config.maxEdgeLen = 48;
    config.maxSimplificationError = 1.3f;
    config.minRegionArea = 8;
    config.mergeRegionArea = 40;
    config.maxVertsPerPoly = 6;
    config.tileSize = tileSize;
    config.borderSize = border;
    config.width = tileSize + border * 2;
    config.height = tileSize + border * 2;
    config.detailSampleDist = cs * 6.0f;
    config.detailSampleMaxError = params.cellHeight * 4.0f;
    const float pad = static_cast<float>(border) * cs;
    config.bmin[0] = static_cast<float>(originX) - pad;
    config.bmin[1] = cell.minHeight - 2.0f;
    config.bmin[2] = static_cast<float>(originZ) - pad;
    config.bmax[0] = static_cast<float>(originX + desc.cellMetres) + pad;
    config.bmax[1] = cell.maxHeight + 4.0f;
    config.bmax[2] = static_cast<float>(originZ + desc.cellMetres) + pad;

    // The surface over the padded bounds comes straight from the heightmap, so
    // neighbours need not be loaded and the border sees the same ground the
    // next tile does.
    const float step = static_cast<float>(desc.metresPerSample);
    const int nx = static_cast<int>(std::ceil((config.bmax[0] - config.bmin[0]) / step)) + 1;
    const int nz = static_cast<int>(std::ceil((config.bmax[2] - config.bmin[2]) / step)) + 1;
    std::vector<float> verts;
    verts.reserve(static_cast<std::size_t>(nx) * nz * 3);
    for (int z = 0; z < nz; ++z) {
        for (int x = 0; x < nx; ++x) {
            const float wx = config.bmin[0] + static_cast<float>(x) * step;
            const float wz = config.bmin[2] + static_cast<float>(z) * step;
            verts.insert(verts.end(), {wx, heightfield.heightAt(wx, wz), wz});
        }
    }
    std::vector<int> tris;
    tris.reserve(static_cast<std::size_t>(nx - 1) * (nz - 1) * 6);
    for (int z = 0; z + 1 < nz; ++z) {
        for (int x = 0; x + 1 < nx; ++x) {
            const int i00 = z * nx + x;
            const int i10 = i00 + 1;
            const int i01 = i00 + nx;
            const int i11 = i01 + 1;
            tris.insert(tris.end(), {i00, i01, i10, i10, i01, i11});
        }
    }
    const int triCount = static_cast<int>(tris.size() / 3);
    const int vertCount = static_cast<int>(verts.size() / 3);
    std::vector<unsigned char> areas(static_cast<std::size_t>(triCount), 0);

    SilentContext ctx;
    Owned<rcHeightfield> solid{rcAllocHeightfield(), rcFreeHeightField};
    if (solid.value == nullptr || !rcCreateHeightfield(&ctx, *solid.value, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch)) {
        return false;
    }
    rcMarkWalkableTriangles(&ctx, config.walkableSlopeAngle, verts.data(), vertCount, tris.data(), triCount, areas.data());
    if (!rcRasterizeTriangles(&ctx, verts.data(), vertCount, tris.data(), areas.data(), triCount, *solid.value, config.walkableClimb)) {
        return false;
    }
    rcFilterLowHangingWalkableObstacles(&ctx, config.walkableClimb, *solid.value);
    rcFilterLedgeSpans(&ctx, config.walkableHeight, config.walkableClimb, *solid.value);
    rcFilterWalkableLowHeightSpans(&ctx, config.walkableHeight, *solid.value);

    Owned<rcCompactHeightfield> compact{rcAllocCompactHeightfield(), rcFreeCompactHeightfield};
    if (compact.value == nullptr || !rcBuildCompactHeightfield(&ctx, config.walkableHeight, config.walkableClimb, *solid.value, *compact.value)
        || !rcErodeWalkableArea(&ctx, config.walkableRadius, *compact.value) || !rcBuildDistanceField(&ctx, *compact.value)
        || !rcBuildRegions(&ctx, *compact.value, config.borderSize, config.minRegionArea, config.mergeRegionArea)) {
        return false;
    }
    Owned<rcContourSet> contours{rcAllocContourSet(), rcFreeContourSet};
    if (contours.value == nullptr || !rcBuildContours(&ctx, *compact.value, config.maxSimplificationError, config.maxEdgeLen, *contours.value)) {
        return false;
    }
    if (contours.value->nconts == 0) {
        return true;
    }
    Owned<rcPolyMesh> polys{rcAllocPolyMesh(), rcFreePolyMesh};
    Owned<rcPolyMeshDetail> detail{rcAllocPolyMeshDetail(), rcFreePolyMeshDetail};
    if (polys.value == nullptr || detail.value == nullptr || !rcBuildPolyMesh(&ctx, *contours.value, config.maxVertsPerPoly, *polys.value)
        || !rcBuildPolyMeshDetail(&ctx, *polys.value, *compact.value, config.detailSampleDist, config.detailSampleMaxError, *detail.value)) {
        return false;
    }
    if (polys.value->npolys == 0) {
        return true;
    }
    for (int i = 0; i < polys.value->npolys; ++i) {
        polys.value->flags[i] = kWalkFlag;
        polys.value->areas[i] = RC_WALKABLE_AREA;
    }

    dtNavMeshCreateParams create{};
    create.verts = polys.value->verts;
    create.vertCount = polys.value->nverts;
    create.polys = polys.value->polys;
    create.polyAreas = polys.value->areas;
    create.polyFlags = polys.value->flags;
    create.polyCount = polys.value->npolys;
    create.nvp = polys.value->nvp;
    create.detailMeshes = detail.value->meshes;
    create.detailVerts = detail.value->verts;
    create.detailVertsCount = detail.value->nverts;
    create.detailTris = detail.value->tris;
    create.detailTriCount = detail.value->ntris;
    create.walkableHeight = params.agentHeight;
    create.walkableRadius = params.agentRadius;
    create.walkableClimb = params.agentClimb;
    create.tileX = id.x;
    create.tileY = id.z;
    create.tileLayer = 0;
    rcVcopy(create.bmin, polys.value->bmin);
    rcVcopy(create.bmax, polys.value->bmax);
    create.cs = config.cs;
    create.ch = config.ch;
    create.buildBvTree = true;

    unsigned char* data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&create, &data, &size)) {
        return false;
    }
    out.data.assign(data, data + size);
    dtFree(data);
    return true;
}

void NavWorld::Deleter::operator()(dtNavMesh* mesh) const
{
    dtFreeNavMesh(mesh);
}

void NavWorld::QueryDeleter::operator()(dtNavMeshQuery* query) const
{
    dtFreeNavMeshQuery(query);
}

NavWorld::NavWorld() = default;
NavWorld::~NavWorld() = default;

bool NavWorld::init(const WorldDesc& desc, std::uint32_t maxTiles)
{
    mesh_.reset(dtAllocNavMesh());
    dtNavMeshParams params{};
    params.tileWidth = static_cast<float>(desc.cellMetres);
    params.tileHeight = static_cast<float>(desc.cellMetres);
    params.maxTiles = static_cast<int>(maxTiles);
    int tileBits = 1;
    while ((1u << tileBits) < maxTiles) {
        ++tileBits;
    }
    params.maxPolys = 1 << std::min(22 - tileBits, 20);
    if (mesh_ == nullptr || dtStatusFailed(mesh_->init(&params))) {
        mesh_.reset();
        return false;
    }
    query_.reset(dtAllocNavMeshQuery());
    if (query_ == nullptr || dtStatusFailed(query_->init(mesh_.get(), 4096))) {
        mesh_.reset();
        query_.reset();
        return false;
    }
    tiles_ = 0;
    return true;
}

bool NavWorld::addTile(CellId id, NavTile&& tile)
{
    if (mesh_ == nullptr || tile.empty()) {
        return false;
    }
    removeTile(id);
    const std::size_t size = tile.data.size();
    // Detour owns the buffer once the tile is added and frees it itself.
    auto* data = static_cast<unsigned char*>(dtAlloc(size, DT_ALLOC_PERM));
    std::copy(tile.data.begin(), tile.data.end(), data);
    if (dtStatusFailed(mesh_->addTile(data, static_cast<int>(size), DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(data);
        return false;
    }
    ++tiles_;
    return true;
}

void NavWorld::removeTile(CellId id)
{
    if (mesh_ == nullptr) {
        return;
    }
    const dtTileRef ref = mesh_->getTileRefAt(id.x, id.z, 0);
    if (ref != 0 && dtStatusSucceed(mesh_->removeTile(ref, nullptr, nullptr))) {
        --tiles_;
    }
}

bool NavWorld::findPath(glm::dvec3 from, glm::dvec3 to, std::vector<glm::dvec3>& path) const
{
    path.clear();
    if (mesh_ == nullptr || query_ == nullptr) {
        return false;
    }
    const float extents[3] = {2.0f, 6.0f, 2.0f};
    dtQueryFilter filter;
    filter.setIncludeFlags(kWalkFlag);
    const float start[3] = {static_cast<float>(from.x), static_cast<float>(from.y), static_cast<float>(from.z)};
    const float end[3] = {static_cast<float>(to.x), static_cast<float>(to.y), static_cast<float>(to.z)};
    dtPolyRef startRef = 0;
    dtPolyRef endRef = 0;
    float startNear[3];
    float endNear[3];
    if (dtStatusFailed(query_->findNearestPoly(start, extents, &filter, &startRef, startNear)) || startRef == 0
        || dtStatusFailed(query_->findNearestPoly(end, extents, &filter, &endRef, endNear)) || endRef == 0) {
        return false;
    }
    constexpr int kMaxPolys = 1024;
    dtPolyRef polys[kMaxPolys];
    int polyCount = 0;
    if (dtStatusFailed(query_->findPath(startRef, endRef, startNear, endNear, &filter, polys, &polyCount, kMaxPolys)) || polyCount == 0
        || polys[polyCount - 1] != endRef) {
        return false; // a partial path ends short of the goal, so it counts as no path
    }
    constexpr int kMaxPoints = 2048;
    float points[kMaxPoints * 3];
    int pointCount = 0;
    if (dtStatusFailed(query_->findStraightPath(startNear, endNear, polys, polyCount, points, nullptr, nullptr, &pointCount, kMaxPoints)) || pointCount == 0) {
        return false;
    }
    for (int i = 0; i < pointCount; ++i) {
        path.emplace_back(points[i * 3], points[i * 3 + 1], points[i * 3 + 2]);
    }
    return true;
}

}
