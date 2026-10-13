#include "ichor/world/streamer.h"

#include "ichor/core/log.h"

#include <glm/geometric.hpp>

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

std::int32_t chebyshev(CellId a, CellId b)
{
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

// One grid at a given sample stride, with a skirt dropped around the edge so
// a coarser neighbour never shows a crack.
Mesh buildLod(const Heightfield& heightfield, const Heightfield::Cell& cell, std::uint32_t stride)
{
    const WorldDesc& desc = heightfield.desc();
    const std::uint32_t side = cell.samplesPerSide;
    const std::uint32_t quads = (side - 1) / stride;
    const std::uint32_t verts = quads + 1;
    const double originX = heightfield.cellOriginX(cell.id);
    const double originZ = heightfield.cellOriginZ(cell.id);
    const float spacing = static_cast<float>(desc.metresPerSample * stride);
    const float skirt = std::max(2.0f, (cell.maxHeight - cell.minHeight) * 0.05f + spacing);

    Mesh mesh;
    mesh.name = "cell";
    mesh.vertices.reserve(static_cast<std::size_t>(verts) * verts + verts * 4);
    const auto vertexAt = [&](std::uint32_t vx, std::uint32_t vz, float drop) {
        Vertex v{};
        const std::uint32_t sx = std::min(vx * stride, side - 1);
        const std::uint32_t sz = std::min(vz * stride, side - 1);
        const float h = cell.heights[static_cast<std::size_t>(sz) * side + sx];
        // Positions are relative to the cell origin; the renderer places the cell as an instance.
        v.position = {static_cast<float>(vx) * spacing, h - drop, static_cast<float>(vz) * spacing};
        v.normal = heightfield.normalAt(originX + v.position.x, originZ + v.position.z);
        v.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        v.uv = {static_cast<float>(originX + v.position.x) / 8.0f, static_cast<float>(originZ + v.position.z) / 8.0f};
        v.weights[0] = 65535;
        mesh.boundsMin = glm::min(mesh.boundsMin, v.position);
        mesh.boundsMax = glm::max(mesh.boundsMax, v.position);
        return v;
    };
    mesh.boundsMin = glm::vec3(1e30f);
    mesh.boundsMax = glm::vec3(-1e30f);
    for (std::uint32_t z = 0; z < verts; ++z) {
        for (std::uint32_t x = 0; x < verts; ++x) {
            mesh.vertices.push_back(vertexAt(x, z, 0.0f));
        }
    }
    for (std::uint32_t z = 0; z < quads; ++z) {
        for (std::uint32_t x = 0; x < quads; ++x) {
            const std::uint32_t i = z * verts + x;
            mesh.indices.insert(mesh.indices.end(), {i, i + verts, i + 1, i + 1, i + verts, i + verts + 1});
        }
    }
    // Skirts: a lowered copy of each edge, stitched to it.
    // Each skirt winds so its face points out of the cell, like the surface points up.
    const auto skirtEdge = [&](auto edgeIndex, auto edgeVertex, bool flip) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (std::uint32_t k = 0; k < verts; ++k) {
            mesh.vertices.push_back(edgeVertex(k));
        }
        for (std::uint32_t k = 0; k < quads; ++k) {
            const std::uint32_t a = edgeIndex(k);
            const std::uint32_t b = edgeIndex(k + 1);
            if (flip) {
                mesh.indices.insert(mesh.indices.end(), {a, b, base + k, b, base + k + 1, base + k});
            } else {
                mesh.indices.insert(mesh.indices.end(), {a, base + k, b, b, base + k, base + k + 1});
            }
        }
    };
    skirtEdge([&](std::uint32_t k) { return k; }, [&](std::uint32_t k) { return vertexAt(k, 0, skirt); }, true);
    skirtEdge([&](std::uint32_t k) { return quads * verts + k; }, [&](std::uint32_t k) { return vertexAt(k, quads, skirt); }, false);
    skirtEdge([&](std::uint32_t k) { return k * verts; }, [&](std::uint32_t k) { return vertexAt(0, k, skirt); }, false);
    skirtEdge([&](std::uint32_t k) { return k * verts + quads; }, [&](std::uint32_t k) { return vertexAt(quads, k, skirt); }, true);
    mesh.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

// A coarser copy of a full-resolution cell mesh that spends its triangles where
// the ground is rough. The cell's edge, and the skirts hanging from it, stay
// exactly as they are, so neighbours at different levels still meet.
Mesh simplifyLod(const Mesh& full, float spacing, std::size_t reduction)
{
    constexpr float kEdge = 0.01f;
    std::vector<unsigned char> lock(full.vertices.size(), 0);
    for (std::size_t i = 0; i < full.vertices.size(); ++i) {
        const glm::vec3& p = full.vertices[i].position;
        const bool edge = p.x < kEdge || p.z < kEdge || p.x > full.boundsMax.x - kEdge || p.z > full.boundsMax.z - kEdge;
        lock[i] = edge ? 1 : 0;
    }
    Mesh mesh;
    mesh.name = full.name;
    mesh.boundsMin = full.boundsMin;
    mesh.boundsMax = full.boundsMax;
    mesh.vertices.resize(full.vertices.size());
    std::vector<std::uint32_t> indices(full.indices.size());
    // An absolute error in metres, growing with the level, so a flat cell collapses
    // as far as its triangle target asks and a rough one stops where it would show.
    const float maxError = 0.3f * spacing * std::sqrt(static_cast<float>(reduction));
    float error = 0.0f;
    const std::size_t kept = meshopt_simplifyWithAttributes(indices.data(), full.indices.data(), full.indices.size(), &full.vertices[0].position.x,
        full.vertices.size(), sizeof(Vertex), nullptr, 0, nullptr, 0, lock.data(), std::max<std::size_t>(full.indices.size() / reduction, 3), maxError, meshopt_SimplifyErrorAbsolute, &error);
    indices.resize(kept);
    mesh.indices = std::move(indices);
    const std::size_t unique = meshopt_optimizeVertexFetch(mesh.vertices.data(), mesh.indices.data(), mesh.indices.size(), full.vertices.data(), full.vertices.size(), sizeof(Vertex));
    mesh.vertices.resize(unique);
    mesh.submeshes.clear();
    mesh.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

}

void WorldStreamer::buildPayload(const Heightfield& heightfield, CellId id, const StreamerConfig& config, CellPayload& out, const Hydrology* hydrology)
{
    const std::uint32_t lodCount = config.lodCount;
    heightfield.cell(id, out.cell);
    out.lods.clear();
    out.nav.data.clear();
    out.plants.clear();
    out.water = Mesh{};
    if (lodCount > 0) {
        out.lods.push_back(buildLod(heightfield, out.cell, 1));
    }
    std::size_t reduction = 1;
    for (std::uint32_t lod = 1; lod < lodCount; ++lod) {
        reduction *= 4;
        out.lods.push_back(simplifyLod(out.lods[0], static_cast<float>(heightfield.desc().metresPerSample), reduction));
    }
    if (config.vegetation) {
        scatterVegetation(heightfield, id, config.plants, out.plants);
    }
    if (config.water && hydrology != nullptr) {
        out.water = buildWaterMesh(heightfield, *hydrology, config.rivers, id, config.waterMesh);
    }
    if (config.navmesh && !buildNavTile(heightfield, id, config.nav, out.nav)) {
        logf(LogLevel::Warn, "navmesh: cell {},{} failed to build", id.x, id.z);
        out.nav.data.clear();
    }
}

WorldStreamer::WorldStreamer(const Heightfield& heightfield, StreamerConfig config)
    : heightfield_(heightfield)
    , config_(config)
{
    // A shared hydrology of another world would put water in the wrong places, so it is not used.
    if (config_.hydrology && config_.hydrology->side != heightfield_.desc().samplesPerSide()) {
        logf(LogLevel::Warn, "the hydrology given to the streamer is for a world of another size and is ignored");
        config_.hydrology.reset();
    }
    if (!config_.synchronous) {
        thread_ = std::thread([this] { worker(); });
    }
}

WorldStreamer::~WorldStreamer()
{
    {
        const std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

const Hydrology* WorldStreamer::activeHydrology() const
{
    if (!config_.water) {
        return nullptr;
    }
    return config_.hydrology ? config_.hydrology.get() : &hydrology_;
}

void WorldStreamer::worker()
{
    // The whole world's drainage takes a while and is only read here, so it is worked out here, off
    // the game thread, before the first cell is built.
    if (config_.water && !config_.hydrology) {
        hydrology_ = computeHydrology(heightfield_);
    }
    for (;;) {
        CellId id;
        std::uint64_t generation = 0;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) {
                return;
            }
            id = queue_.front();
            queue_.pop_front();
            generation = generation_;
        }
        auto payload = std::make_unique<CellPayload>();
        buildPayload(heightfield_, id, config_, *payload, activeHydrology());
        const std::lock_guard lock(mutex_);
        if (generation == generation_) {
            finished_[id] = std::move(payload);
        }
    }
}

void WorldStreamer::request(CellId id)
{
    if (resident_.contains(id) || requested_.contains(id)) {
        return;
    }
    requested_.insert(id);
    if (config_.synchronous) {
        if (config_.water && !config_.hydrology && hydrology_.empty()) {
            hydrology_ = computeHydrology(heightfield_);
        }
        auto payload = std::make_unique<CellPayload>();
        buildPayload(heightfield_, id, config_, *payload, activeHydrology());
        finished_[id] = std::move(payload);
        return;
    }
    {
        const std::lock_guard lock(mutex_);
        queue_.push_back(id);
    }
    wake_.notify_one();
}

void WorldStreamer::update(glm::dvec3 anchor)
{
    const CellId centre = heightfield_.cellAt(anchor.x, anchor.z);

    // Nearest first, so the player's own cell never waits behind the ring.
    std::vector<CellId> wanted;
    for (std::int32_t dz = -config_.radiusCells; dz <= config_.radiusCells; ++dz) {
        for (std::int32_t dx = -config_.radiusCells; dx <= config_.radiusCells; ++dx) {
            const CellId id{centre.x + dx, centre.z + dz};
            if (heightfield_.contains(id)) {
                wanted.push_back(id);
            }
        }
    }
    std::sort(wanted.begin(), wanted.end(), [&](CellId a, CellId b) {
        const auto da = chebyshev(a, centre);
        const auto db = chebyshev(b, centre);
        return da != db ? da < db : a < b;
    });
    // Forget queued builds the player has left behind, so the worker reaches
    // the cells it needs now instead of finishing a trail of stale ones.
    {
        const std::lock_guard lock(mutex_);
        std::erase_if(queue_, [&](CellId id) {
            const bool stale = chebyshev(id, centre) > config_.radiusCells;
            if (stale) requested_.erase(id);
            return stale;
        });
    }
    for (CellId id : wanted) {
        request(id);
    }

    // Apply finished builds, nearest first, up to the budget.
    std::vector<CellId> ready;
    {
        const std::lock_guard lock(mutex_);
        for (const auto& [id, payload] : finished_) {
            ready.push_back(id);
        }
    }
    std::sort(ready.begin(), ready.end(), [&](CellId a, CellId b) {
        const auto da = chebyshev(a, centre);
        const auto db = chebyshev(b, centre);
        return da != db ? da < db : a < b;
    });
    std::uint32_t activated = 0;
    for (CellId id : ready) {
        if (activated >= config_.activationsPerUpdate) {
            break;
        }
        std::unique_ptr<CellPayload> payload;
        {
            const std::lock_guard lock(mutex_);
            payload = std::move(finished_[id]);
            finished_.erase(id);
        }
        requested_.erase(id);
        if (chebyshev(id, centre) > config_.radiusCells + config_.hysteresisCells) {
            continue; // the player moved on while it was building
        }
        resident_.insert(id);
        ++loads_;
        ++activated;
        if (loaded_) {
            loaded_(*payload);
        }
    }

    // Drop what fell out of the ring, with hysteresis so a border walk does not thrash.
    std::vector<CellId> gone;
    for (CellId id : resident_) {
        if (chebyshev(id, centre) > config_.radiusCells + config_.hysteresisCells) {
            gone.push_back(id);
        }
    }
    for (CellId id : gone) {
        resident_.erase(id);
        ++unloads_;
        if (unloaded_) {
            unloaded_(id);
        }
    }
}

bool WorldStreamer::ready(glm::dvec3 anchor) const
{
    const CellId centre = heightfield_.cellAt(anchor.x, anchor.z);
    for (std::int32_t dz = -config_.requiredRadius; dz <= config_.requiredRadius; ++dz) {
        for (std::int32_t dx = -config_.requiredRadius; dx <= config_.requiredRadius; ++dx) {
            const CellId id{centre.x + dx, centre.z + dz};
            if (heightfield_.contains(id) && !resident_.contains(id)) {
                return false;
            }
        }
    }
    return true;
}

std::size_t WorldStreamer::pending() const
{
    // Everything asked for and not yet applied, including the build in progress.
    return requested_.size();
}

void WorldStreamer::clear()
{
    {
        const std::lock_guard lock(mutex_);
        queue_.clear();
        finished_.clear();
        ++generation_;
    }
    requested_.clear();
    for (CellId id : resident_) {
        ++unloads_;
        if (unloaded_) {
            unloaded_(id);
        }
    }
    resident_.clear();
}

}
