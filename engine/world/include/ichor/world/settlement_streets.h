#pragma once

#include "ichor/assets/bundle_types.h"
#include "ichor/world/heightfield.h"
#include "ichor/world/placed_settlements.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ichor {

// SEED-503: the street grid inside a forged settlement, and the lots along it that the houses stand on.
//
// Each settlement gets axis-aligned streets through the middle of its footprint: one street for a hamlet, a
// cross for a village, a 3 by 3 grid for a town and a 5 by 5 grid for a city. The two main streets pass through
// the gate bays of the perimeter wall (the kit centres a gate at -2 m from the grid-snapped centre on every
// side). A street is a straight line with a width; it is dirt, drawn as a strip over the ground by
// SettlementStreetSet::buildMesh and left bare of buildings.
//
// Lots are candidate building footprints along both sides of every street, fronting it: each lot is a box
// whose `facing` is the quarter turn of the house that stands on it (0 faces +z, 1 +x, 2 -z, 3 -x, the kit's
// convention), looking at the street. The layout is pure geometry from the site and the seed: no ground, water
// or road is read here. The placer keeps the lots that stand on good ground, in priority order (the streets
// nearest the centre first), and lays its buildings on them.
//
// Everything depends on the world seed, the site and the parameters only, never on the order settlements are
// visited in.

struct SettlementStreetParams {
    float widthM = 4.0f;         // the dirt surface, the width of the gate bay it leads through
    float frontGapM = 0.8f;      // between a street's edge and the front of a house
    float lotGapM = 1.0f;        // between two houses side by side
    float wallClearM = 6.0f;     // a lot keeps this far inside the perimeter wall
    float lift = 0.14f;          // metres the strip lies over the ground, above the road ribbon's own lift
    float wallMarginM = 8.0f;    // the same value as SettlementKitParams::wallMarginM: the footprint is the wall's square
    float skipChance = 0.12f;    // a lot is left empty this often, so a street is not a wall of houses
    float pitchM[kSettlementTierCount] = {0.0f, 0.0f, 28.0f, 24.0f}; // between parallel streets
    int linesPerAxis[kSettlementTierCount] = {1, 1, 3, 5};
    int crossAxis[kSettlementTierCount] = {0, 1, 1, 1}; // whether the streets across the main axis exist
};

struct Street {
    std::string settlementId;
    bool alongX = true; // runs along x (a street of constant z) or along z
    bool main = false;  // passes through the gates
    double fixed = 0.0; // the constant coordinate (z for alongX, else x)
    double from = 0.0;  // the varying coordinate, from < to
    double to = 0.0;
    float widthM = 0.0f;
    double ax() const { return alongX ? from : fixed; }
    double az() const { return alongX ? fixed : from; }
    double bx() const { return alongX ? to : fixed; }
    double bz() const { return alongX ? fixed : to; }
};

struct StreetLot {
    double x = 0.0; // the centre of the footprint
    double z = 0.0;
    float halfWidthM = 0.0f; // along x
    float halfDepthM = 0.0f; // along z
    std::uint8_t facing = 0; // quarter turns, towards the street
    bool temple = false;     // the lot sized for the settlement's temple
    std::size_t street = 0;  // index into SettlementLayout::streets of the street it fronts
    std::int8_t side = 1;    // which side of it: +1 or -1
    float alongHalfM = 0.0f; // half the frontage, parallel to the street
    float acrossHalfM = 0.0f; // half the depth, away from the street
};

struct SettlementLayout {
    std::string settlementId;
    std::uint8_t tier = 0;
    double centreX = 0.0; // the grid-snapped centre the wall is built around
    double centreZ = 0.0;
    double halfM = 0.0;   // half the side of the wall's square
    std::vector<Street> streets;
    std::vector<StreetLot> lots; // in priority order
};

// Half the side of the square the perimeter wall (and so the settlement's footprint) stands on, a multiple of the
// kit grid: the tier's building radius and the wall margin, rounded up. 0 when the tier is empty.
double settlementHalfM(const SettlementTierParams& tier, float wallMarginM);

class SettlementStreetSet {
public:
    const std::vector<SettlementLayout>& all() const { return layouts_; }
    bool empty() const { return layouts_.empty(); }
    // Null when the settlement has no layout.
    const SettlementLayout* of(const std::string& settlementId) const;

    // The dirt strips of one cell, positions relative to the cell origin, lying over the heightfield. A strip
    // piece belongs to the cell its middle lies in. Pieces on water, off the world or within the road ribbon
    // are left out so no two surfaces are coplanar. uv.x runs across the street from 0 to 1, uv.y along it in
    // street widths, the same mapping as the road ribbon. No street in the cell gives no vertices.
    Mesh buildMesh(const Heightfield& heightfield, CellId id, const RoadField* roads, const WaterField* water) const;

    // How far into a settlement's footprint a point lies: 1 over the streets and the houses' ground, falling to 0
    // beyond the wall's square over `fadeM` metres. The terrain shader tints the ground by it.
    float groundAmountAt(double x, double z) const;

    // The settlement footprints (square, half side) the tint covers, for tests and the streamer.
    struct Footprint {
        double x = 0.0;
        double z = 0.0;
        double halfM = 0.0;
    };
    const std::vector<Footprint>& footprints() const { return footprints_; }

    static constexpr double kFadeM = 10.0;

private:
    friend struct SettlementStreetBuilder;
    std::vector<SettlementLayout> layouts_;
    std::vector<Footprint> footprints_;
    float liftM_ = 0.0f;
    std::map<std::pair<int, int>, std::vector<std::size_t>> byBucket_; // footprints, in 64 m buckets
};

struct SettlementStreets {
    SettlementStreetSet set;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }
};

// Fails, with a message, on non-finite site values, two sites with one id, an empty heightfield or a parameter out
// of range. `tiers` are the building tiers the lots are sized and counted by (PlacedSettlementParams::tiers).
SettlementStreets layoutSettlementStreets(const std::vector<SettlementSite>& sites, const Heightfield& heightfield, const PlacedSettlementParams& tiers,
    const SettlementStreetParams& params = {});

}
