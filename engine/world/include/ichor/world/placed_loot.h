#pragma once

#include "ichor/world/heightfield.h"
#include "ichor/world/placed_pois.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace worldforge {
class ForgedWorld;
struct ContentManifest;
}

namespace ichor {

// The bake step that puts the manifest's loot into the world (doc 08 phase 12, doc 06). The content
// manifest (phase 11) says which item lies at which dungeon or ruin; this turns each of those into one
// loot instance with a position in world metres, standing on the ground near its place, owned by the
// cell that position lies in, ready for the streamer to hand out per cell.
//
// "Loot lives in places": an item is never rolled, never dropped by a corpse and never duplicated.
// Every manifest placement becomes exactly one instance, so the catalogue's uniques and notables
// (doc 06: about 60 and 200) each exist once per world. A manifest that names an item twice, an item
// the catalogue does not know, a place the POI set does not hold, or a place that is not a dungeon or
// landmark (a settlement holds no hidden loot) is refused whole rather than half placed.
//
// The spot is drawn from a random stream keyed by the world seed and the item id alone, inside a disc
// around the place, then kept inside the world (so an item at the world edge may lie closer to its place than minRadiusM). So the result depends on nothing but the inputs: not on
// the order items are listed, nor the order cells load in. Instances are sorted by item id.

constexpr std::size_t kMaxLootItems = 65536;

struct LootItemInfo {
    std::string id;
    std::string tier;      // "unique", "notable" or whatever the catalogue calls a lesser grade
    std::string itemClass; // "gear", "consumable", ...
};

struct PlacedLoot {
    std::string itemId;
    std::string poiId; // the dungeon or landmark it lies at
    std::string tier;
    std::string itemClass;
    double x = 0.0; // world metres
    double z = 0.0;
    float groundY = 0.0f; // metres, the terrain at (x, z)
    CellId cell;
};

struct PlacedLootParams {
    float minRadiusM = 1.5f; // from the place's centre
    float maxRadiusM = 9.0f;
};

class PlacedLootSet {
public:
    const std::vector<PlacedLoot>& all() const { return loot_; }
    bool empty() const { return loot_.empty(); }

    // The loot lying in `id`, sorted by item id; empty for a cell with none or outside the world.
    std::vector<PlacedLoot> inCell(CellId id) const;
    const PlacedLoot* find(const std::string& itemId) const;
    // The same set without the items `drop` says yes to, in the same order and cells.
    PlacedLootSet without(const std::function<bool(const PlacedLoot&)>& drop) const;

private:
    friend struct PlacedLootBuilder;
    std::vector<PlacedLoot> loot_;
    std::map<CellId, std::vector<std::size_t>> byCell_;
};

struct PlacedLootResult {
    PlacedLootSet set;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }
};

// The core: `manifest.items` laid out around `pois`. `catalogue` is the item records (from the
// notable_items and catalogue tables); it may hold items the manifest does not use, but not one id twice.
// Fails, with a message, on an empty heightfield, bad params, an over-long list, or any of the refusals above.
PlacedLootResult placeManifestLoot(const worldforge::ContentManifest& manifest, const PlacedPoiSet& pois, const std::vector<LootItemInfo>& catalogue,
    const Heightfield& heightfield, const PlacedLootParams& params = {});

// The forged world's manifest, at the same grid-to-metres mapping the other bake adapters use. Fails
// when the world did not reach the content phase.
PlacedLootResult placeForgedLoot(const worldforge::ForgedWorld& world, const PlacedPoiSet& pois, const std::vector<LootItemInfo>& catalogue,
    const Heightfield& heightfield, const PlacedLootParams& params = {});

}
