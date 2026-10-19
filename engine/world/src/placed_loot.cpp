#include "ichor/world/placed_loot.h"

#include "ichor/core/rng.h"

#include <worldforge/content_manifest.h>
#include <worldforge/forge_world.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace ichor {

struct PlacedLootBuilder {
    static void index(PlacedLootSet& set, const Heightfield& heightfield, std::vector<PlacedLoot> loot)
    {
        set.loot_ = std::move(loot);
        std::sort(set.loot_.begin(), set.loot_.end(), [](const PlacedLoot& a, const PlacedLoot& b) { return a.itemId < b.itemId; });
        const auto last = static_cast<std::int32_t>(heightfield.desc().cellsPerSide()) - 1;
        for (std::size_t i = 0; i < set.loot_.size(); ++i) {
            PlacedLoot& item = set.loot_[i];
            CellId cell = heightfield.cellAt(item.x, item.z);
            cell.x = std::clamp(cell.x, 0, last);
            cell.z = std::clamp(cell.z, 0, last);
            item.cell = cell;
            set.byCell_[cell].push_back(i);
        }
    }
};

namespace {

PlacedLootResult failure(std::string message)
{
    PlacedLootResult out;
    out.error = std::move(message);
    return out;
}

constexpr int kSpotAttempts = 32;

// A spot in the ring between the radii around (cx, cz). Rejection sampling inside the square, with only
// + - * on doubles, so it draws the same on every compiler; falls back to a diamond of the outer radius.
void spotAround(Rng& rng, double cx, double cz, double minR, double maxR, double& x, double& z)
{
    for (int attempt = 0; attempt < kSpotAttempts; ++attempt) {
        const double dx = (static_cast<double>(rng.nextFloat()) * 2.0 - 1.0) * maxR;
        const double dz = (static_cast<double>(rng.nextFloat()) * 2.0 - 1.0) * maxR;
        const double dxSq = dx * dx;
        const double dzSq = dz * dz;
        const double d2 = dxSq + dzSq;
        if (d2 >= minR * minR && d2 <= maxR * maxR) {
            x = cx + dx;
            z = cz + dz;
            return;
        }
    }
    // A ring too thin for the rejection loop: a point on the diamond |dx| + |dz| = maxR, so items still spread.
    const double along = static_cast<double>(rng.nextFloat()) * 2.0 - 1.0;
    const double sign = (rng.nextU32() & 1u) != 0u ? 1.0 : -1.0;
    const double across = maxR - (along < 0.0 ? -along : along) * maxR;
    x = cx + along * maxR;
    z = cz + sign * across;
}

}

std::vector<PlacedLoot> PlacedLootSet::inCell(CellId id) const
{
    std::vector<PlacedLoot> out;
    if (const auto found = byCell_.find(id); found != byCell_.end()) {
        for (const std::size_t i : found->second) out.push_back(loot_[i]);
    }
    return out;
}

const PlacedLoot* PlacedLootSet::find(const std::string& itemId) const
{
    const auto it = std::lower_bound(loot_.begin(), loot_.end(), itemId, [](const PlacedLoot& p, const std::string& v) { return p.itemId < v; });
    return it != loot_.end() && it->itemId == itemId ? &*it : nullptr;
}

PlacedLootResult placeManifestLoot(const worldforge::ContentManifest& manifest, const PlacedPoiSet& pois, const std::vector<LootItemInfo>& catalogue,
    const Heightfield& heightfield, const PlacedLootParams& params)
{
    if (heightfield.empty() || !heightfield.desc().valid()) return failure("the heightfield is empty");
    if (!(params.minRadiusM >= 0.0f) || !(params.maxRadiusM >= params.minRadiusM) || !std::isfinite(params.maxRadiusM) || params.maxRadiusM > 1000.0f) {
        return failure("the loot radii are negative, reversed or not finite");
    }
    if (manifest.items.size() > kMaxLootItems || catalogue.size() > kMaxLootItems) return failure("too many loot items");

    std::map<std::string, const LootItemInfo*> known;
    for (const LootItemInfo& info : catalogue) {
        if (!known.emplace(info.id, &info).second) return failure(std::format("the catalogue lists {} twice", info.id));
    }

    const double extent = heightfield.desc().sizeMetres;
    const Rng world(manifest.worldSeed);
    std::set<std::string> seen;
    std::vector<PlacedLoot> placed;
    placed.reserve(manifest.items.size());
    for (const worldforge::ItemPlacement& placement : manifest.items) {
        if (!seen.insert(placement.itemId).second) return failure(std::format("the manifest places {} twice", placement.itemId));
        const auto info = known.find(placement.itemId);
        if (info == known.end()) return failure(std::format("the manifest places {}, which the catalogue does not know", placement.itemId));
        const PlacedPoi* poi = pois.find(placement.poiId);
        if (poi == nullptr) return failure(std::format("{} is placed at {}, which is not a placed point of interest", placement.itemId, placement.poiId));
        if (poi->kind == PlacedPoiKind::Settlement) return failure(std::format("{} is placed at {}, a settlement", placement.itemId, placement.poiId));

        Rng rng = world.derive("loot:" + placement.itemId);
        PlacedLoot item;
        spotAround(rng, poi->x, poi->z, params.minRadiusM, params.maxRadiusM, item.x, item.z);
        item.x = std::clamp(item.x, 0.0, extent);
        item.z = std::clamp(item.z, 0.0, extent);
        item.itemId = placement.itemId;
        item.poiId = placement.poiId;
        item.tier = info->second->tier;
        item.itemClass = info->second->itemClass;
        item.groundY = heightfield.heightAt(item.x, item.z);
        placed.push_back(std::move(item));
    }

    PlacedLootResult out;
    PlacedLootBuilder::index(out.set, heightfield, std::move(placed));
    return out;
}

PlacedLootResult placeForgedLoot(const worldforge::ForgedWorld& world, const PlacedPoiSet& pois, const std::vector<LootItemInfo>& catalogue,
    const Heightfield& heightfield, const PlacedLootParams& params)
{
    if (!world.manifest().has_value()) return failure(world.ok() ? "the forged world stopped before the content phase" : world.error());
    return placeManifestLoot(*world.manifest(), pois, catalogue, heightfield, params);
}

}
