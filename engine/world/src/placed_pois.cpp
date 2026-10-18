#include "ichor/world/placed_pois.h"

#include <worldforge/forge_world.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>

namespace ichor {

struct PlacedPoiBuilder {
    static void add(PlacedPoiSet& set, PlacedPoi poi) { set.pois_.push_back(std::move(poi)); }

    static void index(PlacedPoiSet& set, const Heightfield& heightfield)
    {
        std::sort(set.pois_.begin(), set.pois_.end(), [](const PlacedPoi& a, const PlacedPoi& b) { return a.id < b.id; });
        const auto last = static_cast<std::int32_t>(heightfield.desc().cellsPerSide()) - 1;
        for (std::size_t i = 0; i < set.pois_.size(); ++i) {
            PlacedPoi& poi = set.pois_[i];
            CellId cell = heightfield.cellAt(poi.x, poi.z);
            cell.x = std::clamp(cell.x, 0, last);
            cell.z = std::clamp(cell.z, 0, last);
            poi.cell = cell;
            set.byCell_[cell].push_back(i);
        }
    }
};

namespace {

PlacedPois failure(std::string message)
{
    PlacedPois out;
    out.error = std::move(message);
    return out;
}

double metresOf(int cell, int gridSize, double sizeMetres)
{
    return static_cast<double>(cell) * sizeMetres / static_cast<double>(gridSize - 1);
}

// True when `id` is `prefix` plus the digits of a forged index, in the form the forge writes.
bool indexOf(const std::string& id, const std::string& prefix, std::size_t& out)
{
    if (!id.starts_with(prefix) || id.size() == prefix.size() || id.size() > prefix.size() + 9) return false;
    std::size_t value = 0;
    for (std::size_t i = prefix.size(); i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') return false;
        value = value * 10 + static_cast<std::size_t>(id[i] - '0');
    }
    out = value;
    return (prefix == "poi_" ? worldforge::detail::forgePoiId(value) : worldforge::detail::forgeSettlementId(value)) == id;
}

}

std::vector<PlacedPoi> PlacedPoiSet::inCell(CellId id) const
{
    std::vector<PlacedPoi> out;
    if (const auto found = byCell_.find(id); found != byCell_.end()) {
        for (const std::size_t i : found->second) out.push_back(pois_[i]);
    }
    return out;
}

const PlacedPoi* PlacedPoiSet::find(const std::string& poiId) const
{
    const auto it = std::lower_bound(pois_.begin(), pois_.end(), poiId, [](const PlacedPoi& p, const std::string& v) { return p.id < v; });
    return it != pois_.end() && it->id == poiId ? &*it : nullptr;
}

PlacedPois placeForgedPois(const worldforge::ForgedWorld& world, const Heightfield& heightfield, const PlacedPoiParams& params)
{
    if (heightfield.empty() || !heightfield.desc().valid()) return failure("the heightfield is empty");
    if (!world.manifest().has_value() || !world.settlements().has_value() || !world.pois().has_value()) {
        return failure(world.ok() ? "the forged world stopped before the content phase" : world.error());
    }
    if (world.size() < 2) return failure("the forged world has no grid");
    if (!std::isfinite(params.landmarkHeightM) || !std::isfinite(params.dungeonHeightM) || !std::isfinite(params.settlementHeightM)) {
        return failure("a silhouette height is not finite");
    }
    const worldforge::ContentManifest& manifest = *world.manifest();
    const worldforge::SettlementMap& settlements = *world.settlements();
    const worldforge::PoiMap& pois = *world.pois();
    const double extent = heightfield.desc().sizeMetres;

    std::map<std::string, std::string> dungeons;
    for (const worldforge::DungeonSlot& slot : manifest.dungeons) dungeons.emplace(slot.poiId, slot.archetype);

    PlacedPois out;
    for (const std::string& id : manifest.poiIds) {
        PlacedPoi poi;
        poi.id = id;
        int cx = 0;
        int cy = 0;
        std::size_t n = 0;
        if (indexOf(id, "settlement_", n)) {
            if (n >= settlements.sites.size()) return failure(std::format("the manifest lists {} but the world has {} settlements", id, settlements.sites.size()));
            cx = settlements.sites[n].x;
            cy = settlements.sites[n].y;
            poi.kind = PlacedPoiKind::Settlement;
            poi.silhouetteM = params.settlementHeightM;
        } else if (indexOf(id, "poi_", n)) {
            if (n >= pois.pois.size()) return failure(std::format("the manifest lists {} but the world has {} points of interest", id, pois.pois.size()));
            cx = pois.pois[n].x;
            cy = pois.pois[n].y;
            poi.sightlinePermille = pois.pois[n].score;
            const auto dungeon = dungeons.find(id);
            poi.kind = dungeon != dungeons.end() ? PlacedPoiKind::Dungeon : PlacedPoiKind::Landmark;
            if (dungeon != dungeons.end()) poi.archetype = dungeon->second;
            poi.silhouetteM = dungeon != dungeons.end() ? params.dungeonHeightM : params.landmarkHeightM;
        } else {
            return failure(std::format("the manifest lists {}, which is neither a settlement nor a point of interest", id));
        }
        if (cx < 0 || cy < 0 || cx >= world.size() || cy >= world.size()) return failure(std::format("{} lies off the forged grid", id));
        poi.x = metresOf(cx, world.size(), extent);
        poi.z = metresOf(cy, world.size(), extent);
        poi.groundY = heightfield.heightAt(poi.x, poi.z);
        PlacedPoiBuilder::add(out.set, std::move(poi));
    }
    PlacedPoiBuilder::index(out.set, heightfield);
    return out;
}

Mesh placedPoiMarkerMesh(PlacedPoiKind kind, float heightM)
{
    const float h = std::isfinite(heightM) ? std::max(heightM, 0.5f) : 1.0f;
    const float base = kind == PlacedPoiKind::Landmark ? std::max(h * 0.18f, 1.0f) : kind == PlacedPoiKind::Dungeon ? 3.0f : 4.0f;
    const float top = kind == PlacedPoiKind::Landmark ? base * 0.2f : base * 0.7f;
    Mesh mesh;
    mesh.name = kind == PlacedPoiKind::Landmark ? "poi_landmark" : kind == PlacedPoiKind::Dungeon ? "poi_dungeon" : "poi_settlement";
    mesh.boundsMin = {-base, 0.0f, -base};
    mesh.boundsMax = {base, h, base};
    const glm::vec3 corners[4] = {{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}};
    for (int side = 0; side < 4; ++side) {
        const glm::vec3 a = corners[side];
        const glm::vec3 b = corners[(side + 1) % 4];
        const glm::vec3 normal = glm::normalize(glm::vec3(a.x + b.x, 0.0f, a.z + b.z));
        const glm::vec3 points[4] = {{a.x * base, 0.0f, a.z * base}, {b.x * base, 0.0f, b.z * base}, {b.x * top, h, b.z * top}, {a.x * top, h, a.z * top}};
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int v = 0; v < 4; ++v) {
            Vertex vertex{};
            vertex.position = points[v];
            vertex.normal = normal;
            vertex.tangent = {-normal.z, 0.0f, normal.x, 1.0f};
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
