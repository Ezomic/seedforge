#include "ichor/world/water_mesh.h"

#include <algorithm>

namespace ichor {

Mesh buildWaterMesh(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, CellId id, const WaterMeshParams& params)
{
    Mesh mesh;
    const WorldDesc& desc = heightfield.desc();
    if (heightfield.empty() || hydrology.empty() || hydrology.side != desc.samplesPerSide() || !heightfield.contains(id)) {
        return mesh;
    }
    const std::uint32_t perCell = desc.cellMetres / desc.metresPerSample;
    const std::uint32_t side = perCell + 1;
    const std::uint32_t firstX = static_cast<std::uint32_t>(id.x) * perCell;
    const std::uint32_t firstZ = static_cast<std::uint32_t>(id.z) * perCell;

    // Each sample of the cell: whether it holds water and where the surface lies there.
    std::vector<float> level(static_cast<std::size_t>(side) * side);
    std::vector<bool> wet(static_cast<std::size_t>(side) * side, false);
    bool any = false;
    for (std::uint32_t j = 0; j < side; ++j) {
        for (std::uint32_t i = 0; i < side; ++i) {
            const std::uint32_t gx = firstX + i;
            const std::uint32_t gz = firstZ + j;
            const std::size_t at = static_cast<std::size_t>(j) * side + i;
            const float ground = heightfield.heightAtSample(gx, gz);
            if (isLake(hydrology, heightfield, gx, gz, rivers)) {
                wet[at] = true;
                level[at] = hydrology.filled[hydrology.index(gx, gz)];
            } else if (isRiver(hydrology, hydrology.index(gx, gz), rivers)) {
                wet[at] = true;
                level[at] = ground + params.riverSurface;
            } else {
                level[at] = ground - params.submerge;
            }
            any = any || wet[at];
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
            // Counter-clockwise seen from above.
            mesh.indices.insert(mesh.indices.end(), {v00, v01, v10, v10, v01, v11});
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
