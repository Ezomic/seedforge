#include "ichor/world/streamer.h"

#include "ichor/core/log.h"

#include <glm/geometric.hpp>

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
    const auto skirtEdge = [&](auto edgeIndex, auto edgeVertex) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (std::uint32_t k = 0; k < verts; ++k) {
            mesh.vertices.push_back(edgeVertex(k));
        }
        for (std::uint32_t k = 0; k < quads; ++k) {
            const std::uint32_t a = edgeIndex(k);
            const std::uint32_t b = edgeIndex(k + 1);
            mesh.indices.insert(mesh.indices.end(), {a, base + k, b, b, base + k, base + k + 1});
        }
    };
    skirtEdge([&](std::uint32_t k) { return k; }, [&](std::uint32_t k) { return vertexAt(k, 0, skirt); });
    skirtEdge([&](std::uint32_t k) { return quads * verts + k; }, [&](std::uint32_t k) { return vertexAt(k, quads, skirt); });
    skirtEdge([&](std::uint32_t k) { return k * verts; }, [&](std::uint32_t k) { return vertexAt(0, k, skirt); });
    skirtEdge([&](std::uint32_t k) { return k * verts + quads; }, [&](std::uint32_t k) { return vertexAt(quads, k, skirt); });
    mesh.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

}

void WorldStreamer::buildPayload(const Heightfield& heightfield, CellId id, std::uint32_t lodCount, CellPayload& out)
{
    heightfield.cell(id, out.cell);
    out.lods.clear();
    std::uint32_t stride = 1;
    for (std::uint32_t lod = 0; lod < lodCount && (out.cell.samplesPerSide - 1) % stride == 0 && (out.cell.samplesPerSide - 1) / stride >= 2; ++lod) {
        out.lods.push_back(buildLod(heightfield, out.cell, stride));
        stride *= 4;
    }
}

WorldStreamer::WorldStreamer(const Heightfield& heightfield, StreamerConfig config)
    : heightfield_(heightfield)
    , config_(config)
{
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

void WorldStreamer::worker()
{
    for (;;) {
        CellId id;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) {
                return;
            }
            id = queue_.front();
            queue_.pop_front();
        }
        auto payload = std::make_unique<CellPayload>();
        buildPayload(heightfield_, id, config_.lodCount, *payload);
        const std::lock_guard lock(mutex_);
        finished_[id] = std::move(payload);
    }
}

void WorldStreamer::request(CellId id)
{
    if (resident_.contains(id) || requested_.contains(id)) {
        return;
    }
    requested_.insert(id);
    if (config_.synchronous) {
        auto payload = std::make_unique<CellPayload>();
        buildPayload(heightfield_, id, config_.lodCount, *payload);
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
    const std::lock_guard lock(mutex_);
    return queue_.size() + finished_.size() + (requested_.size() > queue_.size() + finished_.size() ? 0 : 0);
}

void WorldStreamer::clear()
{
    {
        const std::lock_guard lock(mutex_);
        queue_.clear();
        finished_.clear();
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
