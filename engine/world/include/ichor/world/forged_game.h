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
#include "ichor/world/water_field.h"

#include <worldforge/forge_world.h>

#include <cstdint>
#include <functional>
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
// blocked (world x, z) rules out a position (a building, say). Deterministic: the nearest valid point
// by a fixed ring order, ties to the lower z then x.
ForgedSpawn findForgedSpawn(const Heightfield& heightfield, const WaterField* water, const std::vector<std::pair<double, double>>& settlements,
    const std::function<bool(double, double)>& blocked = {}, const ForgedSpawnParams& params = {});

struct ForgedGame {
    ForgedGame() = default;
    ForgedGame(const ForgedGame&) = delete;
    ForgedGame& operator=(const ForgedGame&) = delete;

    WorldDesc desc; // forgedWorldDesc of the requested one: the height range and water level of the forge
    std::shared_ptr<const worldforge::ForgedWorld> world;
    Heightfield heightfield; // the roads phase's terrain
    ForgedBiomeField biomes;
    ForgedSpawn spawn;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }

    // A digest of every piece: the same seed and grid always give the same value.
    std::uint64_t stateHash() const;
};

// Builds the game from a finished forge. Fails, with a message, when the result is empty or a bake step
// refuses the world. Never throws.
std::unique_ptr<ForgedGame> buildForgedGame(const ForgeJobResult& result, const WorldDesc& requested);

}
