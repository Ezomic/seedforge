#include "ichor/world/settlement_streets.h"

#include "ichor/world/ribbon_height.h"

#include <worldforge/subseed.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>

namespace ichor {

struct SettlementStreetBuilder {
    static void add(SettlementStreetSet& set, SettlementLayout layout)
    {
        set.footprints_.push_back({layout.centreX, layout.centreZ, layout.halfM});
        set.layouts_.push_back(std::move(layout));
    }

    static void setLift(SettlementStreetSet& set, float lift) { set.liftM_ = lift; }

    static void index(SettlementStreetSet& set)
    {
        for (std::size_t i = 0; i < set.footprints_.size(); ++i) {
            const SettlementStreetSet::Footprint& f = set.footprints_[i];
            const double reach = f.halfM + SettlementStreetSet::kFadeM;
            const int x0 = static_cast<int>(std::floor((f.x - reach) / 64.0));
            const int x1 = static_cast<int>(std::floor((f.x + reach) / 64.0));
            const int z0 = static_cast<int>(std::floor((f.z - reach) / 64.0));
            const int z1 = static_cast<int>(std::floor((f.z + reach) / 64.0));
            for (int bz = z0; bz <= z1; ++bz) {
                for (int bx = x0; bx <= x1; ++bx) set.byBucket_[{bx, bz}].push_back(i);
            }
        }
    }
};

namespace {

constexpr std::uint64_t kStreetDomain = 0x7374726565747331ull;
constexpr double kGridM = 4.0;
constexpr double kMaxHalfM = 400.0;
constexpr double kPieceM = 2.0;
constexpr double kGateShiftM = 2.0; // the kit centres a gate bay this far below the snapped centre
constexpr float kTempleHalfM = 4.2f;
constexpr float kLotHalfCapM = 5.5f;
constexpr std::uint8_t kVillage = 1;
constexpr std::uint8_t kTown = 2;

struct Stream {
    std::uint64_t state;

    std::uint64_t next() { return state = worldforge::splitmix64(state + 0x9e3779b97f4a7c15ull); }
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    double between(double lo, double hi)
    {
        const volatile double scaled = (hi - lo) * unit();
        return lo + scaled;
    }
};

double snapToGrid(double v) { return std::floor(v / kGridM + 0.5) * kGridM; }

SettlementStreets failure(std::string message)
{
    SettlementStreets out;
    out.error = std::move(message);
    return out;
}

bool paramsValid(const SettlementStreetParams& p)
{
    const bool scalars = std::isfinite(p.widthM) && p.widthM > 0.0f && p.widthM <= 32.0f && std::isfinite(p.frontGapM) && p.frontGapM >= 0.0f && std::isfinite(p.lotGapM)
        && p.lotGapM >= 0.0f && std::isfinite(p.wallClearM) && p.wallClearM >= 0.0f && std::isfinite(p.lift) && p.lift >= 0.0f && std::isfinite(p.wallMarginM) && p.wallMarginM >= 0.0f
        && std::isfinite(p.skipChance) && p.skipChance >= 0.0f && p.skipChance <= 1.0f;
    if (!scalars) return false;
    for (std::size_t t = 0; t < kSettlementTierCount; ++t) {
        if (!std::isfinite(p.pitchM[t]) || p.pitchM[t] < 0.0f || p.linesPerAxis[t] < 0 || p.linesPerAxis[t] > 9) return false;
        if (p.linesPerAxis[t] > 1 && p.pitchM[t] < p.widthM * 3.0f) return false;
    }
    return true;
}

// The lot on one side of a street: `along` is the lot's middle along the street, its frontage half `alongHalf`, its depth half `acrossHalf`.
StreetLot makeLot(const Street& street, double side, double along, float alongHalf, float acrossHalf, const SettlementStreetParams& p)
{
    const double offset = static_cast<double>(street.widthM) * 0.5 + static_cast<double>(p.frontGapM) + static_cast<double>(acrossHalf);
    StreetLot lot;
    lot.alongHalfM = alongHalf;
    lot.acrossHalfM = acrossHalf;
    if (street.alongX) {
        lot.x = along;
        lot.z = street.fixed + side * offset;
        lot.halfWidthM = alongHalf;
        lot.halfDepthM = acrossHalf;
        lot.facing = side > 0.0 ? 2 : 0; // a lot on the +z side looks back at the street, towards -z
    } else {
        lot.x = street.fixed + side * offset;
        lot.z = along;
        lot.halfWidthM = acrossHalf;
        lot.halfDepthM = alongHalf;
        lot.facing = side > 0.0 ? 3 : 1;
    }
    return lot;
}

bool lotTouchesStreet(const StreetLot& lot, const Street& street, double band)
{
    const double minX = lot.x - lot.halfWidthM;
    const double maxX = lot.x + lot.halfWidthM;
    const double minZ = lot.z - lot.halfDepthM;
    const double maxZ = lot.z + lot.halfDepthM;
    const double half = static_cast<double>(street.widthM) * 0.5 + band;
    if (street.alongX) return maxX > street.from - band && minX < street.to + band && maxZ > street.fixed - half && minZ < street.fixed + half;
    return maxZ > street.from - band && minZ < street.to + band && maxX > street.fixed - half && minX < street.fixed + half;
}

void layOutSettlement(const SettlementSite& site, const Heightfield& field, const PlacedSettlementParams& tiers, const SettlementStreetParams& p, SettlementStreetSet& out)
{
    const std::uint8_t tier = std::min<std::uint8_t>(site.tier, kSettlementTierCount - 1);
    const SettlementTierParams& t = tiers.tiers[tier];
    const double half = settlementHalfM(t, p.wallMarginM);
    if (!(half >= kGridM) || half > kMaxHalfM) return;

    SettlementLayout layout;
    layout.settlementId = site.id;
    layout.tier = tier;
    layout.centreX = snapToGrid(site.x);
    layout.centreZ = snapToGrid(site.z);
    layout.halfM = half;

    const int lines = p.linesPerAxis[tier];
    const double pitch = static_cast<double>(p.pitchM[tier]);
    const bool stone = tier >= kTown;
    for (const bool alongX : {true, false}) {
        if (!alongX && p.crossAxis[tier] == 0) continue;
        for (int k = -(lines / 2); k <= lines / 2; ++k) {
            const bool main = k == 0;
            const double centre = alongX ? layout.centreX : layout.centreZ;
            const double reach = main ? (stone ? half : half - kGridM) : half - static_cast<double>(p.wallClearM) - kGateShiftM;
            if (reach <= kGridM) continue;
            Street street;
            street.settlementId = site.id;
            street.alongX = alongX;
            street.main = main;
            street.fixed = (alongX ? layout.centreZ : layout.centreX) - kGateShiftM + static_cast<double>(k) * pitch;
            street.from = centre - reach;
            street.to = centre + reach;
            street.widthM = p.widthM;
            layout.streets.push_back(street);
        }
    }

    Stream rng{worldforge::splitmix64(worldforge::splitmix64(field.desc().seed ^ kStreetDomain) ^ worldforge::hashText(site.id))};
    const float minHalf = t.minHalfFootprintM;
    const float maxHalf = std::max(minHalf, std::min(t.maxHalfFootprintM, kLotHalfCapM));
    const double limit = half - static_cast<double>(p.wallClearM);
    for (std::size_t s = 0; s < layout.streets.size(); ++s) {
        const Street& street = layout.streets[s];
        for (const double side : {1.0, -1.0}) {
            double cursor = street.from + kGridM;
            while (true) {
                const auto alongHalf = static_cast<float>(rng.between(minHalf, maxHalf));
                const auto acrossHalf = static_cast<float>(rng.between(minHalf, maxHalf));
                const bool skip = rng.unit() < static_cast<double>(p.skipChance);
                const double middle = cursor + alongHalf;
                if (middle + alongHalf > street.to - kGridM) break;
                cursor = middle + alongHalf + static_cast<double>(p.lotGapM);
                if (skip) continue;
                StreetLot lot = makeLot(street, side, middle, alongHalf, acrossHalf, p);
                lot.street = s;
                lot.side = static_cast<std::int8_t>(side);
                const bool inside = std::abs(lot.x - layout.centreX) + lot.halfWidthM <= limit && std::abs(lot.z - layout.centreZ) + lot.halfDepthM <= limit;
                if (!inside) continue;
                bool clear = true;
                for (std::size_t o = 0; o < layout.streets.size() && clear; ++o) {
                    if (o != s) clear = !lotTouchesStreet(lot, layout.streets[o], 0.5);
                }
                if (clear) layout.lots.push_back(lot);
            }
        }
    }

    // Nearest the middle first, so a settlement short of ground or of its building count is dense at its heart.
    std::vector<std::size_t> order(layout.lots.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    const auto distance = [&](const StreetLot& lot) { return std::hypot(lot.x - layout.centreX, lot.z - layout.centreZ); };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return distance(layout.lots[a]) < distance(layout.lots[b]); });
    std::vector<StreetLot> sorted;
    sorted.reserve(order.size());
    for (const std::size_t i : order) sorted.push_back(layout.lots[i]);
    layout.lots = std::move(sorted);

    // The nearest lot is the temple's, sized for it on the same street and side; it must pass the checks the other lots do, or the lot stays as it was.
    if (tier >= kVillage && !layout.lots.empty()) {
        const StreetLot first = layout.lots.front();
        const Street& fronting = layout.streets[first.street];
        StreetLot temple = makeLot(fronting, static_cast<double>(first.side), fronting.alongX ? first.x : first.z, kTempleHalfM, kTempleHalfM, p);
        temple.temple = true;
        temple.street = first.street;
        temple.side = first.side;
        bool fits = std::abs(temple.x - layout.centreX) + temple.halfWidthM <= limit && std::abs(temple.z - layout.centreZ) + temple.halfDepthM <= limit;
        for (std::size_t o = 0; o < layout.streets.size() && fits; ++o) {
            if (o != first.street) fits = !lotTouchesStreet(temple, layout.streets[o], 0.5);
        }
        if (fits) layout.lots.front() = temple;
    }
    SettlementStreetBuilder::add(out, std::move(layout));
}

struct Corner {
    double x;
    double z;
};

}

double settlementHalfM(const SettlementTierParams& tier, float wallMarginM)
{
    if (!(tier.radiusM > 0.0f) || !std::isfinite(wallMarginM)) return 0.0;
    return std::ceil((static_cast<double>(tier.radiusM) + static_cast<double>(wallMarginM)) / kGridM) * kGridM;
}

const SettlementLayout* SettlementStreetSet::of(const std::string& settlementId) const
{
    const auto found = std::find_if(layouts_.begin(), layouts_.end(), [&](const SettlementLayout& l) { return l.settlementId == settlementId; });
    return found == layouts_.end() ? nullptr : &*found;
}

float SettlementStreetSet::groundAmountAt(double x, double z) const
{
    if (!std::isfinite(x) || !std::isfinite(z)) return 0.0f;
    const auto found = byBucket_.find({static_cast<int>(std::floor(x / 64.0)), static_cast<int>(std::floor(z / 64.0))});
    if (found == byBucket_.end()) return 0.0f;
    double best = 0.0;
    for (const std::size_t i : found->second) {
        const Footprint& f = footprints_[i];
        const double outside = std::max(std::abs(x - f.x), std::abs(z - f.z)) - f.halfM;
        const double t = outside <= 0.0 ? 1.0 : std::clamp(1.0 - outside / kFadeM, 0.0, 1.0);
        best = std::max(best, t * t * (3.0 - 2.0 * t));
    }
    return static_cast<float>(best);
}

Mesh SettlementStreetSet::buildMesh(const Heightfield& heightfield, CellId id, const RoadField* roads, const WaterField* water) const
{
    Mesh mesh;
    if (layouts_.empty() || heightfield.empty() || !heightfield.contains(id)) return mesh;
    const double cellMetres = static_cast<double>(heightfield.desc().cellMetres);
    const double extent = heightfield.desc().sizeMetres;
    const double originX = heightfield.cellOriginX(id);
    const double originZ = heightfield.cellOriginZ(id);
    for (const SettlementLayout& layout : layouts_) {
        if (layout.centreX + layout.halfM < originX || layout.centreX - layout.halfM > originX + cellMetres || layout.centreZ + layout.halfM < originZ
            || layout.centreZ - layout.halfM > originZ + cellMetres) {
            continue;
        }
        for (const Street& street : layout.streets) {
            const double half = static_cast<double>(street.widthM) * 0.5;
            const int pieces = std::max(1, static_cast<int>(std::ceil((street.to - street.from) / kPieceM)));
            const double step = (street.to - street.from) / pieces;
            for (int i = 0; i < pieces; ++i) {
                const double a = street.from + step * i;
                const double b = a + step;
                const double mid = (a + b) * 0.5;
                const double mx = street.alongX ? mid : street.fixed;
                const double mz = street.alongX ? street.fixed : mid;
                if (mx < originX || mx >= originX + cellMetres || mz < originZ || mz >= originZ + cellMetres) continue;
                const Corner corners[4] = {
                    {street.alongX ? a : street.fixed - half, street.alongX ? street.fixed - half : a},
                    {street.alongX ? a : street.fixed + half, street.alongX ? street.fixed + half : a},
                    {street.alongX ? b : street.fixed - half, street.alongX ? street.fixed - half : b},
                    {street.alongX ? b : street.fixed + half, street.alongX ? street.fixed + half : b},
                };
                bool usable = true;
                for (const Corner& c : corners) {
                    if (c.x < 0.0 || c.z < 0.0 || c.x > extent || c.z > extent) usable = false;
                    else if (water != nullptr && water->at(c.x, c.z).wet()) usable = false;
                    else if (roads != nullptr && roads->coverageAt(c.x, c.z) >= 1.0f) usable = false;
                }
                if (!usable || (roads != nullptr && roads->coverageAt(mx, mz) >= 1.0f) || (water != nullptr && water->at(mx, mz).wet())) continue;
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                for (int k = 0; k < 4; ++k) {
                    const Corner& c = corners[k];
                    Vertex v{};
                    v.position = {static_cast<float>(c.x - originX), terrainMeshHeightAt(heightfield, c.x, c.z) + liftM_, static_cast<float>(c.z - originZ)};
                    v.normal = heightfield.normalAt(c.x, c.z);
                    v.tangent = street.alongX ? glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) : glm::vec4(-1.0f, 0.0f, 0.0f, 1.0f);
                    v.uv = {(k & 1) != 0 ? 1.0f : 0.0f, static_cast<float>(((k & 2) != 0 ? b : a) / static_cast<double>(street.widthM))};
                    v.weights[0] = 65535;
                    mesh.vertices.push_back(v);
                }
                // A street along z is the transpose of one along x, so its corners wind the other way.
                if (street.alongX) {
                    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
                } else {
                    mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base + 1, base + 2, base + 3});
                }
            }
        }
    }
    if (mesh.vertices.empty()) return mesh;
    mesh.name = "street";
    mesh.boundsMin = glm::vec3(1e30f);
    mesh.boundsMax = glm::vec3(-1e30f);
    for (const Vertex& v : mesh.vertices) {
        mesh.boundsMin = glm::min(mesh.boundsMin, v.position);
        mesh.boundsMax = glm::max(mesh.boundsMax, v.position);
    }
    raiseOverGround(heightfield, mesh, originX, originZ, liftM_ * 0.5f, 1.0f);
    if (mesh.indices.empty()) return Mesh{};
    mesh.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

SettlementStreets layoutSettlementStreets(const std::vector<SettlementSite>& sites, const Heightfield& heightfield, const PlacedSettlementParams& tiers,
    const SettlementStreetParams& params)
{
    if (heightfield.empty() || !heightfield.desc().valid()) return failure("the heightfield is empty");
    if (!paramsValid(params)) return failure("a street parameter is out of range or not finite");
    for (const SettlementTierParams& t : tiers.tiers) {
        if (!std::isfinite(t.minHalfFootprintM) || !std::isfinite(t.maxHalfFootprintM) || !(t.minHalfFootprintM > 0.0f) || t.maxHalfFootprintM < t.minHalfFootprintM) {
            return failure("a settlement tier's footprint is out of range or not finite");
        }
    }
    for (const SettlementSite& site : sites) {
        if (!std::isfinite(site.x) || !std::isfinite(site.z)) return failure(std::format("{} has a position that is not finite", site.id));
    }
    std::vector<const SettlementSite*> ordered;
    for (const SettlementSite& site : sites) ordered.push_back(&site);
    std::sort(ordered.begin(), ordered.end(), [](const SettlementSite* a, const SettlementSite* b) { return a->id < b->id; });
    if (std::adjacent_find(ordered.begin(), ordered.end(), [](const SettlementSite* a, const SettlementSite* b) { return a->id == b->id; }) != ordered.end()) {
        return failure("two settlement sites share an id");
    }
    SettlementStreets out;
    for (const SettlementSite* site : ordered) layOutSettlement(*site, heightfield, tiers, params, out.set);
    SettlementStreetBuilder::setLift(out.set, params.lift);
    SettlementStreetBuilder::index(out.set);
    return out;
}

}
