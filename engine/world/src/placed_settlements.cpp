#include "ichor/world/placed_settlements.h"

#include <worldforge/forge_world.h>
#include <worldforge/subseed.h>

#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string_view>
#include <vector>

namespace ichor {

struct PlacedBuildingBuilder {
    static void add(PlacedBuildingSet& set, PlacedBuilding building) { set.buildings_.push_back(std::move(building)); }

    static void index(PlacedBuildingSet& set, const Heightfield& heightfield)
    {
        std::sort(set.buildings_.begin(), set.buildings_.end(), [](const PlacedBuilding& a, const PlacedBuilding& b) { return a.id < b.id; });
        const auto last = static_cast<std::int32_t>(heightfield.desc().cellsPerSide()) - 1;
        for (std::size_t i = 0; i < set.buildings_.size(); ++i) {
            PlacedBuilding& building = set.buildings_[i];
            CellId cell = heightfield.cellAt(building.x, building.z);
            cell.x = std::clamp(cell.x, 0, last);
            cell.z = std::clamp(cell.z, 0, last);
            building.cell = cell;
            set.byCell_[cell].push_back(i);
        }
    }
};

namespace {

constexpr double kProbeStepM = 2.0;
constexpr double kMaxRadiusM = 1000.0;
constexpr std::size_t kMaxSites = 4096;
constexpr std::uint64_t kSettlementBuildDomain = 0x626c64676d61736full;

PlacedSettlements failure(std::string message)
{
    PlacedSettlements out;
    out.error = std::move(message);
    return out;
}

bool tierValid(const SettlementTierParams& t)
{
    return t.buildings >= 0 && t.buildings <= kMaxBuildingsPerSettlement && std::isfinite(t.radiusM) && t.radiusM >= 0.0f && t.radiusM <= kMaxRadiusM && std::isfinite(t.minHalfFootprintM)
        && std::isfinite(t.maxHalfFootprintM) && t.minHalfFootprintM > 0.0f && t.maxHalfFootprintM >= t.minHalfFootprintM && std::isfinite(t.minHeightM)
        && std::isfinite(t.maxHeightM) && t.minHeightM > 0.0f && t.maxHeightM >= t.minHeightM;
}

bool paramsValid(const PlacedSettlementParams& p)
{
    for (const SettlementTierParams& t : p.tiers) {
        if (!tierValid(t)) return false;
    }
    return std::isfinite(p.maxHeightDeltaM) && p.maxHeightDeltaM >= 0.0f && std::isfinite(p.roadMarginM) && p.roadMarginM >= 0.0f && std::isfinite(p.buildingGapM)
        && p.buildingGapM >= 0.0f && p.attemptsPerBuilding >= 1 && p.attemptsPerBuilding <= 1000;
}

struct Stream {
    std::uint64_t state;

    std::uint64_t next() { return state = worldforge::splitmix64(state + 0x9e3779b97f4a7c15ull); }
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    // The product is held in a volatile so no compiler fuses it with the add, which would move a position by an ulp on some targets.
    double between(double lo, double hi)
    {
        const volatile double scaled = (hi - lo) * unit();
        return lo + scaled;
    }
};

struct Footprint {
    double x;
    double z;
    double hw;
    double hd;
};

bool overlaps(const Footprint& a, const PlacedBuilding& b, double gap)
{
    return std::abs(a.x - b.x) < a.hw + b.halfWidthM + gap && std::abs(a.z - b.z) < a.hd + b.halfDepthM + gap;
}

// Samples along a span from `from` to `to` no further apart than kProbeStepM, built by repeated addition so no product can be fused into it.
std::vector<double> probes(double from, double to)
{
    const int steps = std::max(1, static_cast<int>(std::ceil((to - from) / kProbeStepM)));
    const double step = (to - from) / steps;
    std::vector<double> out;
    double at = from;
    for (int i = 0; i < steps; ++i, at += step) out.push_back(at);
    out.push_back(to);
    return out;
}

// Whether a box of this footprint can stand here; `lowest` is set to the lowest ground under it.
bool standableImpl(const Footprint& f, const Heightfield& field, const RoadField* roads, const WaterField* water, const PlacedSettlementParams& p, float& lowest)
{
    const double extent = field.desc().sizeMetres;
    const double rx = f.hw + p.roadMarginM;
    const double rz = f.hd + p.roadMarginM;
    if (f.x - rx < 0.0 || f.z - rz < 0.0 || f.x + rx > extent || f.z + rz > extent) return false;
    const float centre = field.heightAt(f.x, f.z);
    if (!std::isfinite(centre)) return false;
    lowest = centre;
    const std::vector<double> xs = probes(f.x - f.hw, f.x + f.hw);
    const std::vector<double> zs = probes(f.z - f.hd, f.z + f.hd);
    for (const double pz : zs) {
        for (const double px : xs) {
            const float h = field.heightAt(px, pz);
            if (!(std::abs(h - centre) <= p.maxHeightDeltaM)) return false;
            lowest = std::min(lowest, h);
            if (water != nullptr && water->at(px, pz).wet()) return false;
        }
    }
    if (roads != nullptr) {
        for (const double pz : probes(f.z - rz, f.z + rz)) {
            for (const double px : probes(f.x - rx, f.x + rx)) {
                if (roads->coverageAt(px, pz) > 0.0f) return false;
            }
        }
    }
    return true;
}

void placeCluster(const SettlementSite& site, const Heightfield& field, const RoadField* roads, const WaterField* water, const PlacedSettlementParams& p,
    PlacedBuildingSet& set)
{
    const std::uint8_t tier = std::min<std::uint8_t>(site.tier, kSettlementTierCount - 1);
    const SettlementTierParams& t = p.tiers[tier];
    Stream rng{worldforge::splitmix64(worldforge::splitmix64(field.desc().seed ^ kSettlementBuildDomain) ^ worldforge::hashText(site.id))};
    std::vector<PlacedBuilding> placed;
    const int attempts = t.buildings * p.attemptsPerBuilding;
    const double radius = t.radiusM;
    for (int a = 0; a < attempts && static_cast<int>(placed.size()) < t.buildings; ++a) {
        const double dx = rng.between(-radius, radius);
        const double dz = rng.between(-radius, radius);
        const Footprint f{site.x + dx, site.z + dz, rng.between(t.minHalfFootprintM, t.maxHalfFootprintM), rng.between(t.minHalfFootprintM, t.maxHalfFootprintM)};
        const auto height = static_cast<float>(rng.between(t.minHeightM, t.maxHeightM));
        if (dx * dx + dz * dz > radius * radius) continue;
        if (std::any_of(placed.begin(), placed.end(), [&](const PlacedBuilding& b) { return overlaps(f, b, p.buildingGapM); })) continue;
        float lowest = 0.0f;
        if (!standableImpl(f, field, roads, water, p, lowest)) continue;
        PlacedBuilding b;
        b.id = std::format("{}_b{:03}", site.id, placed.size());
        b.settlementId = site.id;
        b.tier = tier;
        b.x = f.x;
        b.z = f.z;
        b.groundY = lowest;
        b.halfWidthM = static_cast<float>(f.hw);
        b.halfDepthM = static_cast<float>(f.hd);
        b.heightM = height;
        placed.push_back(std::move(b));
    }
    for (PlacedBuilding& b : placed) PlacedBuildingBuilder::add(set, std::move(b));
}

double metresOf(int cell, int gridSize, double sizeMetres)
{
    return static_cast<double>(cell) * sizeMetres / static_cast<double>(gridSize - 1);
}

}

std::vector<PlacedBuilding> PlacedBuildingSet::inCell(CellId id) const
{
    std::vector<PlacedBuilding> out;
    if (const auto found = byCell_.find(id); found != byCell_.end()) {
        for (const std::size_t i : found->second) out.push_back(buildings_[i]);
    }
    return out;
}

std::vector<PlacedBuilding> PlacedBuildingSet::ofSettlement(const std::string& settlementId) const
{
    std::vector<PlacedBuilding> out;
    for (const PlacedBuilding& b : buildings_) {
        if (b.settlementId == settlementId) out.push_back(b);
    }
    return out;
}

PlacedSettlements placeSettlementBuildings(const std::vector<SettlementSite>& sites, const Heightfield& heightfield, const RoadField* roads,
    const WaterField* water, const PlacedSettlementParams& params)
{
    if (heightfield.empty() || !heightfield.desc().valid()) return failure("the heightfield is empty");
    if (!paramsValid(params)) return failure("a settlement placement parameter is out of range or not finite");
    if (sites.size() > kMaxSites) return failure("too many settlement sites");
    std::vector<std::string_view> ids;
    for (const SettlementSite& site : sites) ids.push_back(site.id);
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return failure("two settlement sites share an id");
    for (const SettlementSite& site : sites) {
        if (!std::isfinite(site.x) || !std::isfinite(site.z)) return failure(std::format("{} has a position that is not finite", site.id));
    }
    PlacedSettlements out;
    for (const SettlementSite& site : sites) placeCluster(site, heightfield, roads, water, params, out.set);
    PlacedBuildingBuilder::index(out.set, heightfield);
    return out;
}

bool forgedSettlementSites(const worldforge::ForgedWorld& world, const Heightfield& heightfield, std::vector<SettlementSite>& sites, std::string& error)
{
    sites.clear();
    if (heightfield.empty() || !heightfield.desc().valid()) {
        error = "the heightfield is empty";
        return false;
    }
    if (!world.settlements().has_value()) {
        error = world.ok() ? "the forged world stopped before the settlements phase" : world.error();
        return false;
    }
    if (world.size() < 2) {
        error = "the forged world has no grid";
        return false;
    }
    const auto& forged = world.settlements()->sites;
    const double extent = heightfield.desc().sizeMetres;
    for (std::size_t i = 0; i < forged.size(); ++i) {
        const worldforge::Settlement& s = forged[i];
        if (s.x < 0 || s.y < 0 || s.x >= world.size() || s.y >= world.size()) {
            error = std::format("settlement {} lies off the forged grid", i);
            return false;
        }
        sites.push_back({worldforge::detail::forgeSettlementId(i), s.tier, metresOf(s.x, world.size(), extent), metresOf(s.y, world.size(), extent)});
    }
    return true;
}

bool footprintStandable(double x, double z, double halfWidthM, double halfDepthM, const Heightfield& heightfield, const RoadField* roads, const WaterField* water,
    const PlacedSettlementParams& params, float& lowest)
{
    return standableImpl(Footprint{x, z, halfWidthM, halfDepthM}, heightfield, roads, water, params, lowest);
}

PlacedSettlements placeForgedSettlements(const worldforge::ForgedWorld& world, const Heightfield& heightfield, const RoadField* roads,
    const WaterField* water, const PlacedSettlementParams& params)
{
    std::vector<SettlementSite> sites;
    std::string error;
    if (!forgedSettlementSites(world, heightfield, sites, error)) return failure(std::move(error));
    return placeSettlementBuildings(sites, heightfield, roads, water, params);
}

Mesh placedBuildingMesh(float halfWidthM, float halfDepthM, float heightM, float foundationM)
{
    const auto clean = [](float v, float fallback) { return std::isfinite(v) ? std::max(v, 0.1f) : fallback; };
    const float hw = clean(halfWidthM, 1.0f);
    const float hd = clean(halfDepthM, 1.0f);
    const float top = clean(heightM, 3.0f);
    const float bottom = -(std::isfinite(foundationM) ? std::max(foundationM, 0.0f) : 0.0f);
    Mesh mesh;
    mesh.name = "settlement_building";
    mesh.boundsMin = {-hw, bottom, -hd};
    mesh.boundsMax = {hw, top, hd};
    struct Face {
        glm::vec3 normal;
        glm::vec3 corners[4];
    };
    const Face faces[5] = {
        {{0, 1, 0}, {{-hw, top, -hd}, {hw, top, -hd}, {hw, top, hd}, {-hw, top, hd}}},
        {{0, 0, -1}, {{-hw, bottom, -hd}, {hw, bottom, -hd}, {hw, top, -hd}, {-hw, top, -hd}}},
        {{1, 0, 0}, {{hw, bottom, -hd}, {hw, bottom, hd}, {hw, top, hd}, {hw, top, -hd}}},
        {{0, 0, 1}, {{hw, bottom, hd}, {-hw, bottom, hd}, {-hw, top, hd}, {hw, top, hd}}},
        {{-1, 0, 0}, {{-hw, bottom, hd}, {-hw, bottom, -hd}, {-hw, top, -hd}, {-hw, top, hd}}},
    };
    for (const Face& face : faces) {
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int v = 0; v < 4; ++v) {
            Vertex vertex{};
            vertex.position = face.corners[v];
            vertex.normal = face.normal;
            vertex.tangent = {-face.normal.z, 0.0f, face.normal.x, 1.0f};
            vertex.uv = {v == 1 || v == 2 ? 1.0f : 0.0f, v >= 2 ? 1.0f : 0.0f};
            vertex.weights[0] = 65535;
            mesh.vertices.push_back(vertex);
        }
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(first + i);
    }
    mesh.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

}
