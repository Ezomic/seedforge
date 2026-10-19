#pragma once

#include "ichor/world/forge_job.h"
#include "ichor/world/forged_biome.h"
#include "ichor/world/forged_heightfield.h"
#include "ichor/world/forged_navmesh.h"
#include "ichor/world/forged_region_tiers.h"
#include "ichor/world/forged_roads.h"
#include "ichor/world/forged_water.h"
#include "ichor/world/heightfield.h"
#include "ichor/world/map_layers.h"
#include "ichor/world/placed_pois.h"
#include "ichor/world/placed_settlements.h"
#include "ichor/world/settlement_kit.h"
#include "ichor/world/water_field.h"

#include <worldforge/forge_world.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ichor {

// The forged world as the app plays it (doc 08 phase 12, the integration of the bake batch): one place
// that turns a finished ForgeJob into everything the app and the streamer read, so main.cpp only hands
// these pieces to the streamer, the renderer and the map screen. The stand-in world never goes through
// here; the app builds one of these only behind --forged.
//
// Every piece is a pure function of the forged world, so one seed and grid size always give the same
// game, whatever thread forged it and however it was watched. stateHash() folds the pieces into one
// number the determinism tests compare.

struct ForgedSpawn {
    bool found = false; // false: no dry, gentle ground was found and (x, z) is the world centre
    double x = 0.0;
    double z = 0.0;
    float groundY = 0.0f;
    double faceX = 0.0; // the point the search aimed at (the settlement, else the world centre): where the view looks
    double faceZ = 0.0;
};

struct ForgedSpawnParams {
    float searchStepMetres = 4.0f;
    float edgeMarginMetres = 32.0f;     // the spawn keeps this far from the world's edge
    float footprintMetres = 6.0f;       // ground within this radius must be dry and gentle
    float minUpY = 0.94f;               // the surface normal's y at and around the spawn (about 20 degrees)
    float minAboveSeaMetres = 1.0f;     // ground height over the water level
};

// Dry land with low slope nearest the target (the settlement closest to the world centre, else the
// centre). `water` and `blocked` are optional: water rules out any wet ground in the footprint, and
// blocked (world x, z) rules out a position (a building, say). Deterministic: the first ring, in
// square rings around the target, that holds a valid point, and the nearest point of that ring.
ForgedSpawn findForgedSpawn(const Heightfield& heightfield, const WaterField* water, const std::vector<std::pair<double, double>>& settlements,
    const std::function<bool(double, double)>& blocked = {}, const ForgedSpawnParams& params = {});

struct ForgedGame {
    ForgedGame() = default;
    ForgedGame(const ForgedGame&) = delete;
    ForgedGame& operator=(const ForgedGame&) = delete;

    WorldDesc desc; // forgedWorldDesc of the requested one: the height range and water level of the forge
    std::shared_ptr<const worldforge::ForgedWorld> world;
    Heightfield heightfield; // the roads phase's terrain
    std::shared_ptr<const ForgedBiomeField> biomes;
    ForgedWater water; // the forged rivers and lakes as the engine's Hydrology, with the parameters to pair it with
    std::unique_ptr<WaterField> waterField; // over `heightfield` and `water`; what the player wades through
    std::shared_ptr<const ForgedRoads> roads;
    std::shared_ptr<const PlacedPoiSet> pois;
    std::shared_ptr<const PlacedBuildingSet> buildings;
    std::vector<SettlementSite> sites; // the settlements in the forge's order, in world metres
    std::shared_ptr<const SettlementKitSet> kit; // the textured pieces that stand in for the buildings
    std::shared_ptr<const NavSurface> navSurface; // water and roads as the navmesh sees them; reads `waterField` and `roads`
    ForgedRegionTiers tiers;
    MapLayerStack mapLayers;
    ForgedSpawn spawn;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }

    // A digest of the forged world's hash, the terrain, the spawn, the placed objects, the road count and the
// region tiers: the same seed and grid always give the same value.
    std::uint64_t stateHash() const;
};

// The grey-box meshes the app draws the placed world with: one road ribbon per cell that has a road, a
// pillar for each point of interest and one unit box for the buildings (scaled per building).
// Mesh indices are positions in `bundle.meshes`; add the renderer's mesh count from before the upload.
struct ForgedScenery {
    SceneBundle bundle;
    std::map<CellId, std::uint32_t> roadMesh;
    std::map<std::string, std::uint32_t> poiMesh; // by PlacedPoi::id, at its own height with its base on y = 0
    bool kitLoaded = false;                  // the textured kit cooked into `bundle`; when false the buildings are drawn as boxes
    KitMeshes kitMesh;                       // by KitPiece, valid when kitLoaded
    std::uint32_t buildingMesh = 0;          // a box 1 m wide and deep and 1 m tall: scale by 2 * half extents and the height
};

ForgedScenery buildForgedScenery(const ForgedGame& game);

// Cooks the settlement kit (the GLBs under `folder`) into the scenery bundle, only the pieces the game uses. On a
// failure (folder missing, a file does not cook) the scenery is left as it was and the buildings stay boxes.
bool addSettlementKit(ForgedScenery& scenery, const ForgedGame& game, const std::filesystem::path& folder);

// Moves the spawn to dry, gentle ground nearest the middle of settlement `index` (in the forge's order), clear of its
// buildings and kit pieces, and aims the view at it. Returns false, leaving the spawn alone, for an index past the end.
bool spawnAtSettlement(ForgedGame& game, std::size_t index);

// Builds the game from a finished forge. Fails, with a message, when the result is empty or a bake step
// refuses the world. Never throws.
std::unique_ptr<ForgedGame> buildForgedGame(const ForgeJobResult& result, const WorldDesc& requested);

}
