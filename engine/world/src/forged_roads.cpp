#include "ichor/world/forged_roads.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

namespace ichor {

namespace {

double distanceSquaredToSegment(double px, double pz, double ax, double az, double bx, double bz)
{
    const double dx = bx - ax;
    const double dz = bz - az;
    const double len2 = dx * dx + dz * dz;
    const double t = len2 > 0.0 ? std::clamp(((px - ax) * dx + (pz - az) * dz) / len2, 0.0, 1.0) : 0.0;
    const double ex = px - (ax + dx * t);
    const double ez = pz - (az + dz * t);
    return ex * ex + ez * ez;
}

// Liang-Barsky: the part of a segment inside the box, as parameters t0 < t1; false when none.
bool clipToBox(double ax, double az, double bx, double bz, double minX, double minZ, double maxX, double maxZ, double& t0, double& t1)
{
    t0 = 0.0;
    t1 = 1.0;
    const double dx = bx - ax;
    const double dz = bz - az;
    const std::array<std::array<double, 2>, 4> edges = {{{-dx, ax - minX}, {dx, maxX - ax}, {-dz, az - minZ}, {dz, maxZ - az}}};
    for (const auto& [p, q] : edges) {
        if (p == 0.0) {
            if (q < 0.0) return false;
            continue;
        }
        const double r = q / p;
        if (p < 0.0) {
            if (r > t1) return false;
            t0 = std::max(t0, r);
        } else {
            if (r < t0) return false;
            t1 = std::min(t1, r);
        }
    }
    return t0 < t1;
}

}

ForgedRoads::ForgedRoads(const worldforge::ForgedWorld& world, const WorldDesc& desc, const ForgedRoadParams& params)
    : params_(params), desc_(desc)
{
    const bool paramsOk = std::isfinite(params.widthMetres) && std::isfinite(params.shoulderMetres) && std::isfinite(params.lift) && params.widthMetres > 0.0f
        && params.shoulderMetres >= 0.0f;
    if (!desc.valid() || !paramsOk || !world.roads().has_value()) return;
    const worldforge::RoadMap& map = *world.roads();
    if (map.width < 2 || map.width != map.height) return;
    grid_ = map.width;
    cellSize_ = static_cast<double>(desc.sizeMetres) / static_cast<double>(grid_ - 1);

    const auto position = [&](std::uint32_t cell) {
        return std::pair{static_cast<double>(cell % static_cast<std::uint32_t>(grid_)) * cellSize_, static_cast<double>(cell / static_cast<std::uint32_t>(grid_)) * cellSize_};
    };

    std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> pairs; // low cell, high cell, edge
    edgeLength_.assign(map.edges.size(), 0.0);
    for (std::size_t e = 0; e < map.edges.size(); ++e) {
        const worldforge::RoadEdge& edge = map.edges[e];
        if (static_cast<std::size_t>(edge.pathBegin) + edge.pathCount > map.pathCells.size()) continue;
        const std::uint32_t* path = map.pathCells.data() + edge.pathBegin;
        for (std::uint32_t i = 0; i + 1 < edge.pathCount; ++i) {
            const auto [ax, az] = position(path[i]);
            const auto [bx, bz] = position(path[i + 1]);
            edgeLength_[e] += std::hypot(bx - ax, bz - az);
            pairs.emplace_back(std::min(path[i], path[i + 1]), std::max(path[i], path[i + 1]), static_cast<std::uint32_t>(e));
        }
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return std::get<0>(a) == std::get<0>(b) && std::get<1>(a) == std::get<1>(b); }), pairs.end());

    buckets_.assign(static_cast<std::size_t>(grid_) * static_cast<std::size_t>(grid_), {});
    for (const auto& [lo, hi, edge] : pairs) {
        const auto [ax, az] = position(lo);
        const auto [bx, bz] = position(hi);
        const std::uint32_t id = static_cast<std::uint32_t>(segments_.size());
        segments_.push_back({ax, az, bx, bz, edge});
        buckets_[lo].push_back(id);
        buckets_[hi].push_back(id);
    }
}

RoadHit ForgedRoads::query(double x, double z) const
{
    RoadHit hit;
    if (segments_.empty() || !std::isfinite(x) || !std::isfinite(z)) return hit;
    const double half = static_cast<double>(params_.widthMetres) * 0.5;
    const double shoulder = static_cast<double>(params_.shoulderMetres);
    const double reach = half + shoulder;
    const int spread = static_cast<int>(std::ceil(reach / cellSize_)) + 1;
    const int cx = static_cast<int>(std::lround(std::clamp(x / cellSize_, 0.0, static_cast<double>(grid_ - 1))));
    const int cz = static_cast<int>(std::lround(std::clamp(z / cellSize_, 0.0, static_cast<double>(grid_ - 1))));

    double best = reach * reach;
    std::uint32_t bestEdge = 0;
    bool found = false;
    for (int j = std::max(0, cz - spread); j <= std::min(grid_ - 1, cz + spread); ++j) {
        for (int i = std::max(0, cx - spread); i <= std::min(grid_ - 1, cx + spread); ++i) {
            for (const std::uint32_t id : buckets_[static_cast<std::size_t>(j) * static_cast<std::size_t>(grid_) + static_cast<std::size_t>(i)]) {
                const Segment& s = segments_[id];
                const double d2 = distanceSquaredToSegment(x, z, s.ax, s.az, s.bx, s.bz);
                if (d2 < best || (found && d2 == best && s.edge < bestEdge)) {
                    best = d2;
                    bestEdge = s.edge;
                    found = true;
                }
            }
        }
    }
    if (!found) return hit;
    const double distance = std::sqrt(best);
    const double coverage = distance <= half ? 1.0 : (shoulder > 0.0 ? 1.0 - (distance - half) / shoulder : 0.0);
    hit.coverage = static_cast<float>(std::clamp(coverage, 0.0, 1.0));
    hit.edge = hit.coverage > 0.0f ? static_cast<int>(bestEdge) : -1;
    return hit;
}

double ForgedRoads::edgeLengthMetres(int edge) const
{
    if (edge < 0 || static_cast<std::size_t>(edge) >= edgeLength_.size()) return 0.0;
    return edgeLength_[static_cast<std::size_t>(edge)];
}

Mesh ForgedRoads::buildMesh(const Heightfield& heightfield, CellId id) const
{
    Mesh mesh;
    if (segments_.empty() || heightfield.empty() || heightfield.desc().sizeMetres != desc_.sizeMetres || !heightfield.contains(id)) return mesh;
    const double cellMetres = static_cast<double>(desc_.cellMetres);
    const double originX = heightfield.cellOriginX(id);
    const double originZ = heightfield.cellOriginZ(id);
    const double step = std::max(1.0, static_cast<double>(desc_.metresPerSample));
    const double half = static_cast<double>(params_.widthMetres) * 0.5;

    const int firstI = std::max(0, static_cast<int>(std::floor(originX / cellSize_)) - 1);
    const int lastI = std::min(grid_ - 1, static_cast<int>(std::ceil((originX + cellMetres) / cellSize_)) + 1);
    const int firstJ = std::max(0, static_cast<int>(std::floor(originZ / cellSize_)) - 1);
    const int lastJ = std::min(grid_ - 1, static_cast<int>(std::ceil((originZ + cellMetres) / cellSize_)) + 1);
    std::vector<std::uint32_t> wanted;
    for (int j = firstJ; j <= lastJ; ++j) {
        for (int i = firstI; i <= lastI; ++i) {
            const auto& bucket = buckets_[static_cast<std::size_t>(j) * static_cast<std::size_t>(grid_) + static_cast<std::size_t>(i)];
            wanted.insert(wanted.end(), bucket.begin(), bucket.end());
        }
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

    const auto vertexAt = [&](double wx, double wz, double sideX, double sideZ, float across, double along) {
        const double px = wx + sideX * half;
        const double pz = wz + sideZ * half;
        Vertex v{};
        v.position = {static_cast<float>(px - originX), heightfield.heightAt(px, pz) + params_.lift, static_cast<float>(pz - originZ)};
        v.normal = heightfield.normalAt(px, pz);
        v.tangent = {static_cast<float>(-sideZ), 0.0f, static_cast<float>(sideX), 1.0f};
        v.uv = {across, static_cast<float>(along)};
        v.weights[0] = 65535;
        return v;
    };

    for (const std::uint32_t sid : wanted) {
        const Segment& s = segments_[sid];
        double t0 = 0.0;
        double t1 = 1.0;
        if (!clipToBox(s.ax, s.az, s.bx, s.bz, originX, originZ, originX + cellMetres, originZ + cellMetres, t0, t1)) continue;
        const double dx = s.bx - s.ax;
        const double dz = s.bz - s.az;
        const double length = std::hypot(dx, dz);
        if (length <= 0.0) continue;
        const double sideX = -dz / length;
        const double sideZ = dx / length;
        const int pieces = std::max(1, static_cast<int>(std::ceil((t1 - t0) * length / step)));
        for (int k = 0; k < pieces; ++k) {
            const double ta = t0 + (t1 - t0) * static_cast<double>(k) / pieces;
            const double tb = t0 + (t1 - t0) * static_cast<double>(k + 1) / pieces;
            const double ax = s.ax + dx * ta;
            const double az = s.az + dz * ta;
            const double bx = s.ax + dx * tb;
            const double bz = s.az + dz * tb;
            const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(vertexAt(ax, az, -sideX, -sideZ, 0.0f, ta * length));
            mesh.vertices.push_back(vertexAt(ax, az, sideX, sideZ, 1.0f, ta * length));
            mesh.vertices.push_back(vertexAt(bx, bz, -sideX, -sideZ, 0.0f, tb * length));
            mesh.vertices.push_back(vertexAt(bx, bz, sideX, sideZ, 1.0f, tb * length));
            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
        }
    }
    if (mesh.vertices.empty()) return mesh;

    mesh.name = "road";
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
