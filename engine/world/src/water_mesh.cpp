#include "ichor/world/water_mesh.h"

#include <algorithm>
#include <optional>

namespace ichor {

namespace {

// Water at a sample, if there is any: a lake at its filled level, a river a little over its bed.
std::optional<float> waterAt(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, const WaterMeshParams& params, std::int64_t gx, std::int64_t gz)
{
    if (gx < 0 || gz < 0 || gx >= hydrology.side || gz >= hydrology.side) {
        return std::nullopt;
    }
    const auto x = static_cast<std::uint32_t>(gx);
    const auto z = static_cast<std::uint32_t>(gz);
    if (isLake(hydrology, heightfield, x, z, rivers)) {
        return hydrology.filled[hydrology.index(x, z)];
    }
    // Over the floor of a basin too deep to be a lake there is no river either: it is dry ground.
    if (hydrology.lake[hydrology.index(x, z)] == 0 && isRiver(hydrology, hydrology.index(x, z), rivers)) {
        return heightfield.heightAtSample(x, z) + params.riverSurface;
    }
    return std::nullopt;
}

}

WaterLevel waterLevelAtSample(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, const WaterMeshParams& params, std::int64_t gx, std::int64_t gz)
{
    if (const auto water = waterAt(heightfield, hydrology, rivers, params, gx, gz)) {
        return {true, *water};
    }
    float sunk = heightfield.heightAtSample(static_cast<std::uint32_t>(gx), static_cast<std::uint32_t>(gz)) - params.submerge;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (const auto beside = waterAt(heightfield, hydrology, rivers, params, gx + dx, gz + dz)) {
                sunk = std::min(sunk, *beside - params.submerge);
            }
        }
    }
    return {false, sunk};
}

Mesh buildWaterMesh(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, CellId id, const WaterMeshParams& params)
{
    Mesh mesh;
    const WorldDesc& desc = heightfield.desc();
    if (heightfield.empty() || hydrology.empty() || hydrology.side != desc.samplesPerSide() || !heightfield.contains(id)) {
        return mesh;
    }
    const std::uint32_t side = desc.samplesPerCellSide();
    const std::uint32_t perCell = side - 1;
    const std::uint32_t firstX = static_cast<std::uint32_t>(id.x) * perCell;
    const std::uint32_t firstZ = static_cast<std::uint32_t>(id.z) * perCell;

    // Each sample of the cell: whether it holds water and where the surface lies there.
    std::vector<float> level(static_cast<std::size_t>(side) * side);
    std::vector<bool> wet(static_cast<std::size_t>(side) * side, false);
    bool any = false;
    for (std::uint32_t j = 0; j < side; ++j) {
        for (std::uint32_t i = 0; i < side; ++i) {
            const std::size_t at = static_cast<std::size_t>(j) * side + i;
            const WaterLevel sample = waterLevelAtSample(heightfield, hydrology, rivers, params, static_cast<std::int64_t>(firstX) + i, static_cast<std::int64_t>(firstZ) + j);
            wet[at] = sample.wet;
            level[at] = sample.level;
            any = any || sample.wet;
        }
    }
    if (!any) {
        return mesh;
    }

    // A vertex per sample that a drawn quad touches, shared between its quads.
    std::vector<std::int64_t> vertexOf(level.size(), -1);
    const float spacing = static_cast<float>(desc.metresPerSample);
    const auto vertexAt = [&](std::uint32_t i, std::uint32_t j) {
        const std::size_t at = static_cast<std::size_t>(j) * side + i;
        if (vertexOf[at] < 0) {
            Vertex v{};
            v.position = {static_cast<float>(i) * spacing, level[at], static_cast<float>(j) * spacing};
            v.normal = {0.0f, 1.0f, 0.0f};
            v.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
            v.weights[0] = 65535;
            // How far the surface stands over the ground here, in metres; the shader fades, tints and foams the
            // shore from it. Negative where the surface is sunk below the bank.
            v.uv = {level[at] - heightfield.heightAtSample(firstX + i, firstZ + j), 0.0f};
            vertexOf[at] = static_cast<std::int64_t>(mesh.vertices.size());
            mesh.vertices.push_back(v);
        }
        return static_cast<std::uint32_t>(vertexOf[at]);
    };
    for (std::uint32_t j = 0; j + 1 < side; ++j) {
        for (std::uint32_t i = 0; i + 1 < side; ++i) {
            const std::size_t a = static_cast<std::size_t>(j) * side + i;
            if (!wet[a] && !wet[a + 1] && !wet[a + side] && !wet[a + side + 1]) {
                continue;
            }
            const std::uint32_t v00 = vertexAt(i, j);
            const std::uint32_t v10 = vertexAt(i + 1, j);
            const std::uint32_t v01 = vertexAt(i, j + 1);
            const std::uint32_t v11 = vertexAt(i + 1, j + 1);
            // Counter-clockwise seen from above. A river that steps across the quad's diagonal is cut
            // along it, so the two wet corners stay joined instead of meeting at a point under the ground.
            if (wet[a] && wet[a + side + 1] && !wet[a + 1] && !wet[a + side]) {
                mesh.indices.insert(mesh.indices.end(), {v00, v01, v11, v00, v11, v10});
            } else {
                mesh.indices.insert(mesh.indices.end(), {v00, v01, v10, v10, v01, v11});
            }
        }
    }

    mesh.name = "water";
    mesh.boundsMin = glm::vec3(1e30f);
    mesh.boundsMax = glm::vec3(-1e30f);
    for (const Vertex& v : mesh.vertices) {
        mesh.boundsMin = glm::min(mesh.boundsMin, v.position);
        mesh.boundsMax = glm::max(mesh.boundsMax, v.position);
    }
    mesh.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

}
