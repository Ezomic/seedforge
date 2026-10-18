#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/world/heightfield.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace worldforge {
class ForgedWorld;
}

namespace ichor {

// The bake step that puts the forged points of interest into the engine's world (doc 08 phase 12).
// libworldforge decides where settlements, dungeons and landmarks stand (phases 7 and 10) and the
// content manifest (phase 11) gives each an id; this turns those into objects with a position in
// world metres, a cell, and a silhouette height, ready for the streamer to hand out per cell.
//
// Position: forged cell (x, y) of the N cell grid sits at x * sizeMetres / (N - 1) metres, the same
// mapping the heightfield adapter uses, so the first and last cell lie on the world's edges. Ground
// is the Heightfield's height at that point.
//
// Kinds: a settlement is a Settlement. A POI the manifest lists a dungeon slot for is a Dungeon with
// that slot's archetype. Every other POI is a Landmark (the manifest does not tell a ruin from a
// landmark, and both are drawn as a silhouette).
//
// Ids are the manifest's. The set is sorted by id and every query returns objects in that order, so
// nothing depends on the order cells load in.

enum class PlacedPoiKind : std::uint8_t { Settlement, Dungeon, Landmark };

struct PlacedPoi {
    std::string id; // "settlement_NNNN" or "poi_NNNN", as in the manifest
    PlacedPoiKind kind = PlacedPoiKind::Landmark;
    std::string archetype; // the dungeon archetype for a Dungeon, else empty
    double x = 0.0;        // world metres
    double z = 0.0;
    float groundY = 0.0f;     // metres, the terrain at (x, z)
    float silhouetteM = 0.0f; // height of the marker above the ground
    CellId cell;
    std::uint16_t sightlinePermille = 0; // phase 10 score; 0 for a settlement
};

struct PlacedPoiParams {
    float landmarkHeightM = 24.0f;
    float dungeonHeightM = 8.0f;
    float settlementHeightM = 12.0f;
};

class PlacedPoiSet {
public:
    const std::vector<PlacedPoi>& all() const { return pois_; }
    bool empty() const { return pois_.empty(); }

    // The objects standing in `id`, sorted by id; empty for a cell with none or outside the world.
    std::vector<PlacedPoi> inCell(CellId id) const;
    const PlacedPoi* find(const std::string& poiId) const;

private:
    friend struct PlacedPoiBuilder;
    std::vector<PlacedPoi> pois_;
    std::map<CellId, std::vector<std::size_t>> byCell_;
};

struct PlacedPois {
    PlacedPoiSet set;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }
};

// Places every id of the world's manifest. Fails, with a message, when the world did not reach the
// content phase, when the heightfield is empty, or when a manifest id has no settlement or POI behind
// it (so a manifest that does not match its world never half-places).
PlacedPois placeForgedPois(const worldforge::ForgedWorld& world, const Heightfield& heightfield, const PlacedPoiParams& params = {});

// A placeholder silhouette for a kind: a four-sided tapering pillar `heightM` tall, base at y = 0.
Mesh placedPoiMarkerMesh(PlacedPoiKind kind, float heightM);

}
