#include "forged_world_fixture.h"

#include "ichor/world/forged_heightfield.h"
#include "ichor/world/forged_navmesh.h"
#include "ichor/world/forged_water.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

test::ForgedVariant lowRiverThreshold()
{
    test::ForgedVariant variant;
    variant.riverThreshold = 48;
    return variant;
}

constexpr int kGrid = 257;
constexpr std::uint64_t kFirstSeed = 14; // seeds 1 to 13 have no lake or river wide enough to cross; each one skipped is a full forge
constexpr float kFord = 0.5f;

struct Crossing {
    double z = 0.0;
    double fromX = 0.0;
    double toX = 0.0;
    double lakeFromX = 0.0;
    double lakeToX = 0.0;
};

class StripRoad final : public RoadField {
public:
    StripRoad(double z, double fromX, double toX)
        : z_(z)
        , fromX_(fromX)
        , toX_(toX)
    {
    }
    float coverageAt(double x, double z) const override { return std::abs(z - z_) <= 3.0 && x >= fromX_ && x <= toX_ ? 1.0f : 0.0f; }

private:
    double z_;
    double fromX_;
    double toX_;
};

struct Fixture {
    Fixture(const WorldDesc& d, ForgeJobResult forged)
        : desc(d)
        , world(std::move(forged.world))
        , field(std::move(forged.heightfield))
    {
    }
    WorldDesc desc;
    std::shared_ptr<const worldforge::ForgedWorld> world;
    Heightfield field;
    ForgedWater water;
    std::unique_ptr<WaterField> waterField;
    std::vector<Crossing> crossings; // longest stretch of open water first, dry ground a little way past each end
};

std::unique_ptr<Fixture> makeFixtureOwned(std::uint64_t seed)
{
    WorldDesc base;
    base.seed = seed;
    base.sizeMetres = 2048;
    base.metresPerSample = 8;
    base.cellMetres = 512;
    const WorldDesc desc = forgedWorldDesc(base);
    auto owned = std::make_unique<Fixture>(desc, test::forgedWorldFixture(base, kGrid, lowRiverThreshold()));
    Fixture& f = *owned;
    f.water = waterFromForgedWorld(*f.world, f.field);
    REQUIRE(f.water.ok());
    f.waterField = std::make_unique<WaterField>(f.field, f.water.hydrology, f.water.rivers);

    const double step = f.desc.metresPerSample;
    const int samples = static_cast<int>(f.desc.samplesPerSide());
    const double margin = 24.0;
    for (int sz = 8; sz < samples - 8; ++sz) {
        const double z = sz * step;
        int sx = 4;
        while (sx < samples - 4) {
            if (!(f.waterField->at(sx * step, z).depth > kFord)) {
                ++sx;
                continue;
            }
            int end = sx;
            while (end < samples - 4 && f.waterField->at(end * step, z).depth > kFord) {
                ++end;
            }
            Crossing c{z, sx * step - margin, (end - 1) * step + margin, sx * step, (end - 1) * step};
            const bool dryEnds = f.waterField->at(c.fromX, z).depth == 0.0f && f.waterField->at(c.toX, z).depth == 0.0f;
            if (dryEnds && end - sx >= 6 && c.fromX > 0.0 && c.toX < f.desc.sizeMetres) {
                f.crossings.push_back(c);
            }
            sx = end;
        }
    }
    std::stable_sort(f.crossings.begin(), f.crossings.end(), [](const Crossing& a, const Crossing& b) { return a.lakeToX - a.lakeFromX > b.lakeToX - b.lakeFromX; });
    if (f.crossings.size() > 12) {
        f.crossings.resize(12);
    }
    return owned;
}

std::unique_ptr<Fixture> makeFixtureWithCrossings()
{
    for (std::uint64_t seed = kFirstSeed; seed < kFirstSeed + 40; ++seed) {
        auto owned = makeFixtureOwned(seed);
        if (!owned->crossings.empty()) return owned;
    }
    FAIL("no seed has a lake or river wide enough to cross");
    return makeFixtureOwned(kFirstSeed);
}

std::unique_ptr<NavWorld> bakeNav(const Fixture& f, const RoadField* roads, bool respectWater)
{
    auto nav = std::make_unique<NavWorld>();
    REQUIRE(nav->init(f.desc, 64));
    const NavParams params;
    for (std::int32_t z = 0; z < static_cast<std::int32_t>(f.desc.cellsPerSide()); ++z) {
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(f.desc.cellsPerSide()); ++x) {
            NavTile tile;
            const CellId id{x, z};
            REQUIRE((respectWater ? buildForgedNavTile(f.field, *f.waterField, roads, id, params, tile) : buildNavTile(f.field, id, params, tile)));
            if (!tile.empty()) {
                REQUIRE(nav->addTile(id, std::move(tile)));
            }
        }
    }
    return nav;
}

bool pathFor(const Fixture& f, const NavWorld& nav, const Crossing& c, std::vector<glm::dvec3>& path)
{
    const glm::dvec3 from{c.fromX, f.field.heightAt(c.fromX, c.z), c.z};
    const glm::dvec3 to{c.toX, f.field.heightAt(c.toX, c.z), c.z};
    return nav.findPath(from, to, path);
}

// The deepest water the path stands in, sampled every metre along each leg.
float deepestWater(const Fixture& f, const std::vector<glm::dvec3>& path)
{
    float deepest = 0.0f;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const glm::dvec3 a = path[i];
        const glm::dvec3 b = path[i + 1];
        const int steps = std::max(1, static_cast<int>(glm::length(b - a)));
        for (int s = 0; s <= steps; ++s) {
            const glm::dvec3 p = a + (b - a) * (static_cast<double>(s) / steps);
            deepest = std::max(deepest, f.waterField->at(p.x, p.z).depth);
        }
    }
    return deepest;
}

}

TEST_CASE("a path over the forged navmesh does not cross open lake water, and one over the plain navmesh does")
{
    const auto owned = makeFixtureWithCrossings();
    const Fixture& f = *owned;
    REQUIRE(!f.crossings.empty());
    const auto plain = bakeNav(f, nullptr, false);
    const auto forged = bakeNav(f, nullptr, true);

    int compared = 0;
    for (const Crossing& c : f.crossings) {
        std::vector<glm::dvec3> plainPath;
        if (!pathFor(f, *plain, c, plainPath)) {
            continue;
        }
        ++compared;
        CHECK(deepestWater(f, plainPath) > kFord);
        std::vector<glm::dvec3> path;
        if (pathFor(f, *forged, c, path)) {
            CHECK(deepestWater(f, path) <= kFord + 0.15f);
        }
    }
    REQUIRE(compared >= 1);
}

TEST_CASE("a road over the water is a bridge: the path crosses where the road does")
{
    const auto owned = makeFixtureWithCrossings();
    const Fixture& f = *owned;
    REQUIRE(!f.crossings.empty());
    const Crossing& c = f.crossings.front();
    const StripRoad road(c.z, c.fromX, c.toX);
    const auto nav = bakeNav(f, &road, true);

    std::vector<glm::dvec3> path;
    REQUIRE(pathFor(f, *nav, c, path));
    CHECK(deepestWater(f, path) > kFord);
    for (const glm::dvec3& p : path) {
        if (p.x > c.lakeFromX && p.x < c.lakeToX) {
            CHECK(std::abs(p.z - c.z) <= 4.0);
        }
    }
}

TEST_CASE("the surface blocks open water, lets a road bridge it, and leaves dry ground walkable")
{
    const auto owned = makeFixtureWithCrossings();
    const Fixture& f = *owned;
    const ForgedNavSurface surface(*f.waterField, nullptr);
    const Crossing& c = f.crossings.front();
    CHECK(surface.areaAt(c.fromX, c.z) == kNavAreaGround);
    CHECK(surface.areaAt((c.lakeFromX + c.lakeToX) / 2.0, c.z) == kNavAreaBlocked);
    const StripRoad road(c.z, c.fromX, c.toX);
    const ForgedNavSurface bridged(*f.waterField, &road);
    CHECK(bridged.areaAt((c.lakeFromX + c.lakeToX) / 2.0, c.z) == kNavAreaRoad);
}
