#include "ichor/world/ribbon_height.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace ichor {

float terrainMeshHeightAt(const Heightfield& heightfield, double x, double z)
{
    const double s = std::max(1.0, static_cast<double>(heightfield.desc().metresPerSample));
    const double gx = x / s;
    const double gz = z / s;
    const double ix = std::floor(gx);
    const double iz = std::floor(gz);
    const float fx = static_cast<float>(gx - ix);
    const float fz = static_cast<float>(gz - iz);
    const float h00 = heightfield.heightAt(ix * s, iz * s);
    const float h10 = heightfield.heightAt((ix + 1.0) * s, iz * s);
    const float h01 = heightfield.heightAt(ix * s, (iz + 1.0) * s);
    if (fx + fz <= 1.0f) return h00 + fx * (h10 - h00) + fz * (h01 - h00);
    const float h11 = heightfield.heightAt((ix + 1.0) * s, (iz + 1.0) * s);
    return h11 + (1.0f - fx) * (h01 - h11) + (1.0f - fz) * (h10 - h11);
}

void raiseOverGround(const Heightfield& heightfield, Mesh& mesh, double originX, double originZ, float clearM, float maxRaiseM)
{
    // The terrain is linear on every face of the lines x = k s, z = k s and x + z = k s, so a flat triangle's lowest point over it is at
    // one of its corners, where one of its edges crosses such a line, or at a terrain sample inside it. Those are checked exactly.
    const double s = std::max(1.0, static_cast<double>(heightfield.desc().metresPerSample));
    std::vector<float> need(mesh.vertices.size(), 0.0f);
    std::vector<std::uint32_t> kept;
    kept.reserve(mesh.indices.size());
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const std::uint32_t ids[3] = {mesh.indices[t], mesh.indices[t + 1], mesh.indices[t + 2]};
        struct Corner {
            double x;
            double z;
            float y;
        };
        const Corner c[3] = {
            {originX + mesh.vertices[ids[0]].position.x, originZ + mesh.vertices[ids[0]].position.z, mesh.vertices[ids[0]].position.y},
            {originX + mesh.vertices[ids[1]].position.x, originZ + mesh.vertices[ids[1]].position.z, mesh.vertices[ids[1]].position.y},
            {originX + mesh.vertices[ids[2]].position.x, originZ + mesh.vertices[ids[2]].position.z, mesh.vertices[ids[2]].position.y},
        };
        float worst = 0.0f;
        const auto probe = [&](double x, double z, float plane) { worst = std::max(worst, terrainMeshHeightAt(heightfield, x, z) + clearM - plane); };
        for (const Corner& corner : c) probe(corner.x, corner.z, corner.y);
        for (int e = 0; e < 3; ++e) {
            const Corner& a = c[e];
            const Corner& b = c[(e + 1) % 3];
            const double qa[3] = {a.x / s, a.z / s, (a.x + a.z) / s};
            const double qb[3] = {b.x / s, b.z / s, (b.x + b.z) / s};
            for (int family = 0; family < 3; ++family) {
                if (qa[family] == qb[family]) continue;
                const double lo = std::min(qa[family], qb[family]);
                const double hi = std::max(qa[family], qb[family]);
                for (double k = std::floor(lo) + 1.0; k < hi; k += 1.0) {
                    const double u = (k - qa[family]) / (qb[family] - qa[family]);
                    probe(a.x + (b.x - a.x) * u, a.z + (b.z - a.z) * u, a.y + static_cast<float>(u) * (b.y - a.y));
                }
            }
        }
        const double minX = std::min({c[0].x, c[1].x, c[2].x});
        const double maxX = std::max({c[0].x, c[1].x, c[2].x});
        const double minZ = std::min({c[0].z, c[1].z, c[2].z});
        const double maxZ = std::max({c[0].z, c[1].z, c[2].z});
        const double area = (c[1].x - c[0].x) * (c[2].z - c[0].z) - (c[2].x - c[0].x) * (c[1].z - c[0].z);
        if (std::abs(area) > 1e-9) {
            for (double nz = std::ceil(minZ / s); nz * s <= maxZ; nz += 1.0) {
                for (double nx = std::ceil(minX / s); nx * s <= maxX; nx += 1.0) {
                    const double px = nx * s;
                    const double pz = nz * s;
                    const double w1 = ((px - c[0].x) * (c[2].z - c[0].z) - (c[2].x - c[0].x) * (pz - c[0].z)) / area;
                    const double w2 = ((c[1].x - c[0].x) * (pz - c[0].z) - (px - c[0].x) * (c[1].z - c[0].z)) / area;
                    const double w0 = 1.0 - w1 - w2;
                    if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;
                    probe(px, pz, static_cast<float>(w0 * c[0].y + w1 * c[1].y + w2 * c[2].y));
                }
            }
        }
        if (worst > maxRaiseM) continue;
        kept.insert(kept.end(), ids, ids + 3);
        for (const std::uint32_t id : ids) need[id] = std::max(need[id], worst);
    }
    mesh.indices = std::move(kept);
    std::map<std::pair<std::int64_t, std::int64_t>, float> shared;
    const auto key = [](const Vertex& v) { return std::pair{static_cast<std::int64_t>(std::llround(v.position.x * 1000.0f)), static_cast<std::int64_t>(std::llround(v.position.z * 1000.0f))}; };
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        float& at = shared[key(mesh.vertices[i])];
        at = std::max(at, need[i]);
    }
    for (Vertex& v : mesh.vertices) {
        v.position.y += shared[key(v)];
        mesh.boundsMax.y = std::max(mesh.boundsMax.y, v.position.y);
    }
}

}
