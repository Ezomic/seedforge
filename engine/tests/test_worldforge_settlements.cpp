#include <worldforge/settlement_map.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;

struct World {
    HydrologyMap hydrology;
    ClimateMap climate;
    ResourceMap resources;
};

World makeWorld(std::uint64_t seed, int w, int h, int plates)
{
    auto t = generateTectonics(seed, { w, h, plates });
    REQUIRE(t.has_value());
    auto e = erodeTectonics(*t);
    REQUIRE(e.has_value());
    auto hy = computeHydrology(*e, { .riverThreshold = 24 });
    REQUIRE(hy.has_value());
    auto cl = generateClimate(*e);
    REQUIRE(cl.has_value());
    auto rs = generateResources({ &*t, &*e, {} }, { 12, 24, 4, 16 });
    REQUIRE(rs.has_value());
    return { *hy, *cl, *rs };
}

SettlementInputs inputs(const World& w, std::span<const std::uint8_t> biome = {})
{
    return { &w.hydrology, &w.climate, &w.resources, biome };
}

SettlementMap settle(const World& w, const SettlementParams& p = {})
{
    auto s = generateSettlements(inputs(w), p);
    REQUIRE(s.has_value());
    return *s;
}

bool waterNear(const HydrologyMap& h, int x, int y, int radius)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= h.width || ny >= h.height || std::abs(dx) + std::abs(dy) > radius) continue;
            const std::size_t n = h.index(nx, ny);
            if (h.river[n] != 0 || h.lakeDepthM[n] != 0) return true;
        }
    }
    return false;
}

int slopeAt(const HydrologyMap& h, int x, int y)
{
    const int z = h.heightM[h.index(x, y)];
    int s = 0;
    if (x > 0) s = std::max(s, std::abs(z - h.heightM[h.index(x - 1, y)]));
    if (x + 1 < h.width) s = std::max(s, std::abs(z - h.heightM[h.index(x + 1, y)]));
    if (y > 0) s = std::max(s, std::abs(z - h.heightM[h.index(x, y - 1)]));
    if (y + 1 < h.height) s = std::max(s, std::abs(z - h.heightM[h.index(x, y + 1)]));
    return s;
}

std::int64_t closestPairSq(const SettlementMap& s)
{
    std::int64_t closest = std::numeric_limits<std::int64_t>::max();
    for (std::size_t i = 0; i < s.sites.size(); ++i) {
        for (std::size_t j = i + 1; j < s.sites.size(); ++j) {
            const std::int64_t dx = s.sites[i].x - s.sites[j].x;
            const std::int64_t dy = s.sites[i].y - s.sites[j].y;
            closest = std::min(closest, dx * dx + dy * dy);
        }
    }
    return closest;
}

}

TEST_CASE("settlements are deterministic and match the golden hash", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 128, 128, 10);
    const SettlementMap a = settle(w);
    const SettlementMap b = settle(w);
    CHECK(hashSettlementMap(a) == hashSettlementMap(b));
    CHECK(a.sites.size() == b.sites.size());
    CHECK(hashSettlementMap(a) == 0x531a200cc4d68b38ull);
}

TEST_CASE("different seeds give different settlements", "[worldforge][settlements]")
{
    const World a = makeWorld(1, 96, 96, 8);
    const World b = makeWorld(2, 96, 96, 8);
    CHECK(hashSettlementMap(settle(a)) != hashSettlementMap(settle(b)));
}

TEST_CASE("settlements fill the target count on a healthy world", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 256, 256, 12);
    const SettlementMap s = settle(w, { .maxSettlements = 20, .minSpacing = 16 });
    CHECK(s.sites.size() == 20);
}

TEST_CASE("sites respect the minimum spacing", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 256, 256, 12);
    for (const int spacing : { 1, 8, 24, 60 }) {
        const SettlementMap s = settle(w, { .maxSettlements = 100, .minSpacing = spacing });
        REQUIRE(s.sites.size() > 1);
        CHECK(closestPairSq(s) >= static_cast<std::int64_t>(spacing) * spacing);
    }
}

TEST_CASE("a larger radius places fewer sites", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 256, 256, 12);
    const std::size_t tight = settle(w, { .maxSettlements = 256, .minSpacing = 6 }).sites.size();
    const std::size_t wide = settle(w, { .maxSettlements = 256, .minSpacing = 40 }).sites.size();
    CHECK(tight > wide);
    CHECK(wide >= 2);
}

TEST_CASE("sites prefer water and gentle ground over the land average", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 256, 256, 12);
    const SettlementMap s = settle(w, { .maxSettlements = 40, .minSpacing = 10 });
    REQUIRE(s.sites.size() >= 20);

    const HydrologyMap& h = w.hydrology;
    std::int64_t landCells = 0;
    std::int64_t landWet = 0;
    std::int64_t landSlope = 0;
    for (int y = 0; y < h.height; ++y) {
        for (int x = 0; x < h.width; ++x) {
            if (h.heightM[h.index(x, y)] <= 0) continue;
            ++landCells;
            landWet += waterNear(h, x, y, 6) ? 1 : 0;
            landSlope += slopeAt(h, x, y);
        }
    }
    std::int64_t siteWet = 0;
    std::int64_t siteSlope = 0;
    for (const Settlement& site : s.sites) {
        siteWet += waterNear(h, site.x, site.y, 6) ? 1 : 0;
        siteSlope += slopeAt(h, site.x, site.y);
    }
    const std::int64_t n = static_cast<std::int64_t>(s.sites.size());
    CHECK(siteWet * landCells > landWet * n * 3 / 2);
    CHECK(siteSlope * landCells * 2 < landSlope * n);
}

TEST_CASE("sites stand on dry land away from rivers and lakes", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 128, 128, 10);
    const SettlementMap s = settle(w, { .maxSettlements = 60, .minSpacing = 4 });
    REQUIRE_FALSE(s.sites.empty());
    for (const Settlement& site : s.sites) {
        const std::size_t c = s.index(site.x, site.y);
        CHECK(w.hydrology.heightM[c] > 0);
        CHECK(w.hydrology.river[c] == 0);
        CHECK(w.hydrology.lakeDepthM[c] == 0);
        CHECK(site.score == s.suitability[c]);
        CHECK(site.score >= 48);
    }
}

TEST_CASE("the flood axiom floor is a hard constraint", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 256, 256, 12);
    const SettlementMap open = settle(w, { .maxSettlements = 60, .minSpacing = 4 });
    const SettlementMap flood = settle(w, { .minElevationM = 210, .maxSettlements = 60, .minSpacing = 4 });
    int lowOpen = 0;
    for (const Settlement& site : open.sites) lowOpen += w.hydrology.heightM[open.index(site.x, site.y)] < 210 ? 1 : 0;
    REQUIRE(lowOpen > 0);
    REQUIRE_FALSE(flood.sites.empty());
    for (const Settlement& site : flood.sites) CHECK(w.hydrology.heightM[flood.index(site.x, site.y)] >= 210);
    for (std::size_t c = 0; c < flood.suitability.size(); ++c) {
        if (w.hydrology.heightM[c] < 210) CHECK(flood.suitability[c] == 0);
    }
}

TEST_CASE("an unreachable floor yields no sites rather than an error", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 96, 96, 8);
    const SettlementMap s = settle(w, { .minElevationM = kTectonicsMaxHeightM });
    CHECK(s.sites.empty());
}

TEST_CASE("density and traversal multipliers shape the layout", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 256, 256, 12);
    const SettlementParams base{ .maxSettlements = 40, .minSpacing = 12 };
    const std::size_t normal = settle(w, base).sites.size();
    REQUIRE(normal == 40);

    SettlementParams sparse = base;
    sparse.densityMult256 = 128;
    CHECK(settle(w, sparse).sites.size() == normal / 2);

    SettlementParams none = base;
    none.densityMult256 = 0;
    CHECK(settle(w, none).sites.empty());

    SettlementParams fast = base;
    fast.maxSettlements = 256;
    fast.traversalSpacingMult256 = 768;
    SettlementParams slow = fast;
    slow.traversalSpacingMult256 = 256;
    const SettlementMap far = settle(w, fast);
    CHECK(far.sites.size() < settle(w, slow).sites.size());
    CHECK(closestPairSq(far) >= 36 * 36);
}

TEST_CASE("higher scores never get a smaller tier and the best sites come first", "[worldforge][settlements]")
{
    bool sawSpread = false;
    for (std::uint64_t seed = kSeed; seed < kSeed + 6; ++seed) {
        const World w = makeWorld(seed, 256, 256, 12);
        const SettlementMap s = settle(w, { .maxSettlements = 100, .minSpacing = 6 });
        REQUIRE(s.sites.size() > 10);
        for (std::size_t i = 1; i < s.sites.size(); ++i) {
            CHECK(s.sites[i - 1].score >= s.sites[i].score);
            CHECK(s.sites[i - 1].tier >= s.sites[i].tier);
        }
        sawSpread = sawSpread || s.sites.front().tier > s.sites.back().tier;
    }
    CHECK(sawSpread);
}

TEST_CASE("the biome term shifts suitability and neutral matches absent", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 96, 96, 8);
    const std::size_t cells = w.hydrology.heightM.size();
    const std::vector<std::uint8_t> neutral(cells, kNeutralBiomeSuitability);
    const std::vector<std::uint8_t> hostile(cells, 0);
    const SettlementMap absent = settle(w);
    auto withNeutral = generateSettlements(inputs(w, neutral));
    auto withHostile = generateSettlements(inputs(w, hostile));
    REQUIRE(withNeutral.has_value());
    REQUIRE(withHostile.has_value());
    CHECK(hashSettlementMap(absent) == hashSettlementMap(*withNeutral));
    std::int64_t a = 0;
    std::int64_t b = 0;
    for (std::size_t c = 0; c < cells; ++c) {
        a += absent.suitability[c];
        b += withHostile->suitability[c];
    }
    CHECK(b < a);
}

TEST_CASE("gentle slope preference lowers the suitability of steep land only", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 256, 256, 12);
    const SettlementMap plain = settle(w);
    const SettlementMap gentle = settle(w, { .preferGentleSlope = true });
    std::int64_t steepPlain = 0;
    std::int64_t steepGentle = 0;
    std::int64_t flatPlain = 0;
    std::int64_t flatGentle = 0;
    for (int y = 0; y < w.hydrology.height; ++y) {
        for (int x = 0; x < w.hydrology.width; ++x) {
            const std::size_t c = plain.index(x, y);
            if (plain.suitability[c] == 0) continue;
            const bool steep = slopeAt(w.hydrology, x, y) > 20;
            (steep ? steepPlain : flatPlain) += plain.suitability[c];
            (steep ? steepGentle : flatGentle) += gentle.suitability[c];
        }
    }
    REQUIRE(steepPlain > 0);
    CHECK(steepGentle < steepPlain);
    CHECK(flatGentle > flatPlain);
}

TEST_CASE("malformed inputs and parameters are rejected", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 64, 64, 6);
    CHECK_FALSE(generateSettlements({}).has_value());
    CHECK_FALSE(generateSettlements({ &w.hydrology, nullptr, &w.resources, {} }).has_value());

    const std::vector<std::uint8_t> shortBiome(10, 0);
    CHECK_FALSE(generateSettlements(inputs(w, shortBiome)).has_value());

    World seedMismatch = w;
    seedMismatch.climate.seed ^= 1;
    CHECK_FALSE(generateSettlements(inputs(seedMismatch)).has_value());

    World truncated = w;
    truncated.resources.arable.pop_back();
    CHECK_FALSE(generateSettlements(inputs(truncated)).has_value());

    World hostile = w;
    hostile.hydrology.heightM[5] = 30000;
    CHECK_FALSE(generateSettlements(inputs(hostile)).has_value());

    World tiny = w;
    tiny.hydrology.width = 4;
    CHECK_FALSE(generateSettlements(inputs(tiny)).has_value());

    const SettlementParams bad[] = {
        { .seaLevelM = 20000 },
        { .minElevationM = -20000 },
        { .maxSettlements = -1 },
        { .maxSettlements = 257 },
        { .minSpacing = 0 },
        { .minSpacing = 2049 },
        { .minScore = 0 },
        { .minScore = 256 },
        { .densityMult256 = -1 },
        { .densityMult256 = 1025 },
        { .traversalSpacingMult256 = 0 },
        { .traversalSpacingMult256 = 1025 },
        { .minSpacing = std::numeric_limits<int>::max() },
        { .maxSettlements = std::numeric_limits<int>::min() },
    };
    for (const SettlementParams& p : bad) CHECK_FALSE(generateSettlements(inputs(w), p).has_value());
}

TEST_CASE("extreme but valid parameters stay bounded", "[worldforge][settlements]")
{
    const World w = makeWorld(kSeed, 64, 64, 6);
    const SettlementMap huge = settle(w, { .maxSettlements = 256, .minSpacing = 2048, .densityMult256 = 1024, .traversalSpacingMult256 = 1024 });
    CHECK(huge.sites.size() <= 1);
    const SettlementMap dense = settle(w, { .maxSettlements = 256, .minSpacing = 1, .minScore = 1, .densityMult256 = 1024 });
    CHECK(dense.sites.size() <= 256);
    const SettlementMap picky = settle(w, { .minScore = 255 });
    CHECK(picky.sites.size() <= 20);
}
