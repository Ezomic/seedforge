#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/traversal/roads.h"
#include "ichor/world/heightfield.h"
#include "ichor/world/water_field.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace worldforge {
class ForgedWorld;
}

namespace ichor {

// The bake step that fills the forged settlements with grey-box building clusters (doc 08 phase 12).
// libworldforge picks where settlements stand and gives each a size tier (hamlet, village, town, city);
// this turns every one into axis-aligned boxes standing around its centre, in world metres.
//
// A tier sets how many buildings a settlement tries to place, how wide a disc they spread over and how
// big each is. A building is kept only on ground that is flat across its footprint (every sampled
// point within maxHeightDeltaM of the centre's height), dry, clear of every road (a margin included),
// inside the world, and not touching another building of the settlement. A settlement on bad ground
// ends up with fewer buildings than its tier asks for, possibly none; nothing is forced.
//
// Each settlement draws from its own random stream, keyed by the world seed and the settlement id, and
// roads and water are only read, so the result depends on nothing but the inputs: not on the order
// settlements are placed in nor the order cells load in. Buildings are sorted by id; a building is
// owned by the cell its centre lies in.

constexpr std::size_t kSettlementTierCount = 4; // hamlet, village, town, city, as worldforge::SettlementTier
constexpr int kMaxBuildingsPerSettlement = 256;

struct PlacedBuilding {
    std::string id;           // "<settlement id>_bNNN"
    std::string settlementId; // "settlement_NNNN"
    std::uint8_t tier = 0;    // worldforge::SettlementTier of the settlement
    double x = 0.0;           // world metres, the centre of the footprint
    double z = 0.0;
    float groundY = 0.0f;     // metres, the lowest ground under the footprint, where the box stands
    float halfWidthM = 0.0f;  // half the footprint along x
    float halfDepthM = 0.0f;  // half the footprint along z
    float heightM = 0.0f;     // above groundY
    CellId cell;
};

struct SettlementSite {
    std::string id;
    std::uint8_t tier = 0; // values past City are treated as City
    double x = 0.0;
    double z = 0.0;
};

struct SettlementTierParams {
    int buildings = 0;    // how many the settlement tries to place
    float radiusM = 0.0f; // the half-extent of the square, and the radius of the disc, they spread over
    float minHalfFootprintM = 0.0f;
    float maxHalfFootprintM = 0.0f;
    float minHeightM = 0.0f;
    float maxHeightM = 0.0f;
};

struct PlacedSettlementParams {
    SettlementTierParams tiers[kSettlementTierCount] = {
        {3, 24.0f, 2.5f, 3.5f, 3.0f, 4.5f},
        {8, 44.0f, 2.5f, 4.0f, 3.0f, 6.0f},
        {18, 72.0f, 3.0f, 5.0f, 4.0f, 9.0f},
        {36, 110.0f, 3.0f, 7.0f, 5.0f, 16.0f},
    };
    float maxHeightDeltaM = 1.2f; // the footprint may not rise or fall more than this from its centre
    float roadMarginM = 2.0f;     // clearance kept beyond the road's own shoulder
    float buildingGapM = 1.5f;    // clearance between two buildings
    int attemptsPerBuilding = 12;
};

class PlacedBuildingSet {
public:
    const std::vector<PlacedBuilding>& all() const { return buildings_; }
    bool empty() const { return buildings_.empty(); }

    // The buildings standing in `id`, sorted by id; empty for a cell with none or outside the world.
    std::vector<PlacedBuilding> inCell(CellId id) const;
    std::vector<PlacedBuilding> ofSettlement(const std::string& settlementId) const;

private:
    friend struct PlacedBuildingBuilder;
    std::vector<PlacedBuilding> buildings_;
    std::map<CellId, std::vector<std::size_t>> byCell_;
};

struct PlacedSettlements {
    PlacedBuildingSet set;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }
};

// The core: clusters for explicit sites. `roads` and `water` may be null (nothing to keep clear of).
// Fails, with a message, on an empty heightfield, bad params, two sites with one id, or a site whose position is not finite.
// Roads, water and slope are probed over the whole footprint (and the road margin) every 2 m, so a thin road or creek cannot slip between samples.
PlacedSettlements placeSettlementBuildings(const std::vector<SettlementSite>& sites, const Heightfield& heightfield, const RoadField* roads,
    const WaterField* water, const PlacedSettlementParams& params = {});

// The forged world's settlements (phase 7), at the same grid-to-metres mapping the other bake adapters
// use. Fails when the world did not reach the settlements phase or the heightfield is empty.
PlacedSettlements placeForgedSettlements(const worldforge::ForgedWorld& world, const Heightfield& heightfield, const RoadField* roads,
    const WaterField* water, const PlacedSettlementParams& params = {});

// A placeholder building: a box with a flat roof, `foundationM` below y = 0 up to `heightM`, centred on
// the origin in x and z. Five faces (no floor), 20 vertices.
Mesh placedBuildingMesh(float halfWidthM, float halfDepthM, float heightM, float foundationM = 0.5f);

}
