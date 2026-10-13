#include "ichor/world/water_mesh.h"

#include <algorithm>
#include <optional>

namespace ichor {

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

    // Where the water lies at a sample of the world, if there is any: a lake at its filled
    // level, a river a little over its bed.
    const auto waterAt = [&](std::int64_t gx, std::int64_t gz) -> std::optional<float> {
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
    };

    // Each sample of the cell: whether it holds water and where the surface lies there. A dry
    // sample is sunk below its own ground and below the water beside it, so the surface passes
    // under a bank that is lower than the water and never hangs past it.
    std::vector<float> level(static_cast<std::size_t>(side) * side);
    std::vector<bool> wet(static_cast<std::size_t>(side) * side, false);
    bool any = false;
    for (std::uint32_t j = 0; j < side; ++j) {
        for (std::uint32_t i = 0; i < side; ++i) {
            const std::int64_t gx = static_cast<std::int64_t>(firstX) + i;
            const std::int64_t gz = static_cast<std::int64_t>(firstZ) + j;
            const std::size_t at = static_cast<std::size_t>(j) * side + i;
            if (const auto water = waterAt(gx, gz)) {
                wet[at] = true;
                level[at] = *water;
                any = true;
                continue;
            }
            float sunk = heightfield.heightAtSample(static_cast<std::uint32_t>(gx), static_cast<std::uint32_t>(gz)) - params.submerge;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (const auto beside = waterAt(gx + dx, gz + dz)) {
                        sunk = std::min(sunk, *beside - params.submerge);
                    }
                }
            }
            level[at] = sunk;
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
