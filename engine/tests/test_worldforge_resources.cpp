#include <worldforge/resource_map.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;

struct World {
    TectonicMap tectonics;
    ErodedMap eroded;
};

World makeWorld(std::uint64_t seed, int w, int h, int plates)
{
    auto t = generateTectonics(seed, { w, h, plates });
    REQUIRE(t.has_value());
    auto e = erodeTectonics(*t);
    REQUIRE(e.has_value());
    return { *t, *e };
}

ResourceMap resources(const World& w, const ResourceParams& p = {}, std::span<const std::uint8_t> moisture = {})
{
    auto r = generateResources({ &w.tectonics, &w.eroded, moisture }, p);
    REQUIRE(r.has_value());
    return *r;
}

bool nearBoundary(const TectonicMap& t, int x, int y, BoundaryKind kind, int radius)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= t.width || ny >= t.height) continue;
            if (std::abs(dx) + std::abs(dy) <= radius && t.boundaryAt(nx, ny) == kind) return true;
        }
    }
    return false;
}

}

TEST_CASE("resources are deterministic and match the golden hash", "[worldforge][resources]")
{
    const World w = makeWorld(kSeed, 128, 128, 10);
    const ResourceParams params{ 12, 24, 8, 16 };
    const ResourceMap a = resources(w, params);
    const ResourceMap b = resources(w, params);
    CHECK(hashResourceMap(a) == hashResourceMap(b));
    CHECK(a.oreGrade == b.oreGrade);
    CHECK(hashResourceMap(a) == 0x48cf4dc5e746944cull);
}

TEST_CASE("different seeds give different resources", "[worldforge][resources]")
{
    const World a = makeWorld(1, 96, 96, 8);
    const World b = makeWorld(2, 96, 96, 8);
    CHECK(hashResourceMap(resources(a)) != hashResourceMap(resources(b)));
}

TEST_CASE("ore concentrates on convergent boundaries", "[worldforge][resources]")
{
    const World w = makeWorld(kSeed, 192, 192, 14);
    const ResourceMap r = resources(w);
    std::int64_t nearSum = 0, nearCount = 0, farSum = 0, farCount = 0;
    for (int y = 0; y < w.tectonics.height; ++y) {
        for (int x = 0; x < w.tectonics.width; ++x) {
            const std::size_t c = r.index(x, y);
            if (w.eroded.heightM[c] < 0) continue;
            if (nearBoundary(w.tectonics, x, y, BoundaryKind::Convergent, 4)) {
                nearSum += r.oreGrade[c];
                ++nearCount;
            } else if (!nearBoundary(w.tectonics, x, y, BoundaryKind::Convergent, 12) && !nearBoundary(w.tectonics, x, y, BoundaryKind::Divergent, 12)) {
                farSum += r.oreGrade[c];
                ++farCount;
            }
        }
    }
    REQUIRE(nearCount > 0);
    REQUIRE(farCount > 0);
    const double nearMean = static_cast<double>(nearSum) / static_cast<double>(nearCount);
    const double farMean = static_cast<double>(farSum) / static_cast<double>(farCount);
    CHECK(nearMean > 20.0);
    CHECK(farMean == 0.0);
    CHECK(nearMean > farMean + 20.0);
}

TEST_CASE("orichalcum sits on convergent boundaries and iron on divergent ones", "[worldforge][resources]")
{
    const World w = makeWorld(kSeed, 192, 192, 14);
    const ResourceMap r = resources(w);
    int orichalcum = 0, iron = 0;
    for (int y = 0; y < w.tectonics.height; ++y) {
        for (int x = 0; x < w.tectonics.width; ++x) {
            const std::size_t c = r.index(x, y);
            const auto kind = static_cast<OreKind>(r.oreKind[c]);
            CHECK((kind == OreKind::None) == (r.oreGrade[c] == 0));
            if (kind == OreKind::Orichalcum) {
                ++orichalcum;
                CHECK(nearBoundary(w.tectonics, x, y, BoundaryKind::Convergent, 12));
            } else if (kind == OreKind::Iron) {
                ++iron;
                CHECK(nearBoundary(w.tectonics, x, y, BoundaryKind::Divergent, 12));
            }
        }
    }
    CHECK(orichalcum > 0);
    CHECK(iron > 0);
}

TEST_CASE("ore never appears on ocean cells", "[worldforge][resources]")
{
    const World w = makeWorld(kSeed, 128, 128, 10);
    const ResourceMap r = resources(w);
    for (std::size_t c = 0; c < r.oreGrade.size(); ++c) {
        if (w.eroded.heightM[c] < 0) {
            CHECK(r.oreGrade[c] == 0);
            CHECK(r.arable[c] == 0);
        }
    }
}

TEST_CASE("ore reach and grade threshold shape the ore", "[worldforge][resources]")
{
    const World w = makeWorld(kSeed, 128, 128, 10);
    const auto total = [](const ResourceMap& r) {
        std::int64_t s = 0;
        for (const auto g : r.oreGrade) s += g;
        return s;
    };
    CHECK(total(resources(w, { 20, 24, 0, 0 })) > total(resources(w, { 6, 24, 0, 0 })));
    CHECK(total(resources(w, { 12, 0, 0, 0 })) > total(resources(w, { 12, 200, 0, 0 })));
}

TEST_CASE("arable score prefers flat lowland over steep or high ground", "[worldforge][resources]")
{
    const World w = makeWorld(kSeed, 192, 192, 14);
    const ResourceMap r = resources(w);
    std::int64_t lowSum = 0, lowCount = 0, highSum = 0, highCount = 0;
    for (std::size_t c = 0; c < r.arable.size(); ++c) {
        const int h = w.eroded.heightM[c];
        if (h < 0) continue;
        if (h <= 600) {
            lowSum += r.arable[c];
            ++lowCount;
        } else if (h >= 2400) {
            highSum += r.arable[c];
            ++highCount;
        }
    }
    REQUIRE(lowCount > 0);
    const double lowMean = static_cast<double>(lowSum) / static_cast<double>(lowCount);
    CHECK(lowMean > 40.0);
    if (highCount > 0) CHECK(highSum == 0);
}

TEST_CASE("arable score follows the moisture input", "[worldforge][resources]")
{
    const World w = makeWorld(kSeed, 96, 96, 8);
    const std::size_t cells = w.tectonics.heightM.size();
    const std::vector<std::uint8_t> dry(cells, 0);
    const std::vector<std::uint8_t> good(cells, 160);
    const std::vector<std::uint8_t> flooded(cells, 255);
    const auto total = [](const ResourceMap& r) {
        std::int64_t s = 0;
        for (const auto a : r.arable) s += a;
        return s;
    };
    const std::int64_t tDry = total(resources(w, {}, dry));
    const std::int64_t tGood = total(resources(w, {}, good));
    const std::int64_t tWet = total(resources(w, {}, flooded));
    const std::int64_t tNeutral = total(resources(w));
    CHECK(tDry == 0);
    CHECK(tGood > tNeutral);
    CHECK(tNeutral > tWet);
    CHECK(tWet > tDry);
}

TEST_CASE("erosion deposits raise the arable score", "[worldforge][resources]")
{
    World w = makeWorld(kSeed, 64, 64, 6);
    for (auto& h : w.tectonics.heightM) h = 100;
    for (auto& h : w.eroded.heightM) h = 100;
    const std::size_t c = w.eroded.index(32, 32);
    const ResourceMap flat = resources(w);
    w.eroded.heightM[c] = 100;
    w.tectonics.heightM[c] = 98;
    const ResourceMap silted = resources(w);
    CHECK(silted.arable[c] > flat.arable[c]);
}

TEST_CASE("ley nodes sit on boundaries, respect spacing and count", "[worldforge][resources]")
{
    const World w = makeWorld(kSeed, 192, 192, 14);
    const ResourceMap off = resources(w);
    CHECK(off.leyNodes.empty());

    const ResourceMap r = resources(w, { 12, 24, 10, 20 });
    CHECK(!r.leyNodes.empty());
    CHECK(r.leyNodes.size() <= 10);
    for (std::size_t i = 0; i < r.leyNodes.size(); ++i) {
        CHECK(w.tectonics.boundaryAt(r.leyNodes[i].x, r.leyNodes[i].y) != BoundaryKind::None);
        for (std::size_t j = i + 1; j < r.leyNodes.size(); ++j) {
            const int dx = r.leyNodes[i].x - r.leyNodes[j].x;
            const int dy = r.leyNodes[i].y - r.leyNodes[j].y;
            CHECK(dx * dx + dy * dy >= 20 * 20);
        }
    }
}

TEST_CASE("ley nodes cap at what the spacing allows", "[worldforge][resources]")
{
    const World w = makeWorld(kSeed, 64, 64, 6);
    const ResourceMap r = resources(w, { 12, 24, 64, 2048 });
    CHECK(r.leyNodes.size() == 1);
}

TEST_CASE("resources reject malformed inputs and parameters", "[worldforge][resources][adversarial]")
{
    const World w = makeWorld(kSeed, 64, 64, 6);
    const std::size_t cells = w.tectonics.heightM.size();
    const auto gen = [&](const World& world, const ResourceParams& p, std::span<const std::uint8_t> m = {}) {
        return generateResources({ &world.tectonics, &world.eroded, m }, p);
    };

    CHECK(!generateResources({ nullptr, &w.eroded, {} }).has_value());
    CHECK(!generateResources({ &w.tectonics, nullptr, {} }).has_value());

    CHECK(!gen(w, { 0, 24, 0, 0 }).has_value());
    CHECK(!gen(w, { 256, 24, 0, 0 }).has_value());
    CHECK(!gen(w, { 12, -1, 0, 0 }).has_value());
    CHECK(!gen(w, { 12, 256, 0, 0 }).has_value());
    CHECK(!gen(w, { 12, 24, -1, 0 }).has_value());
    CHECK(!gen(w, { 12, 24, 65, 0 }).has_value());
    CHECK(!gen(w, { 12, 24, 1, -1 }).has_value());
    CHECK(!gen(w, { 12, 24, 1, 2049 }).has_value());
    CHECK(!gen(w, { INT32_MIN, INT32_MAX, INT32_MAX, INT32_MIN }).has_value());

    const std::vector<std::uint8_t> shortMoisture(cells - 1, 100);
    CHECK(!gen(w, {}, shortMoisture).has_value());

    World otherSeed = w;
    otherSeed.eroded.seed ^= 1;
    CHECK(!gen(otherSeed, {}).has_value());

    World wrongSize = w;
    wrongSize.eroded.heightM.pop_back();
    CHECK(!gen(wrongSize, {}).has_value());

    World wrongGrid = w;
    wrongGrid.eroded.width = 32;
    CHECK(!gen(wrongGrid, {}).has_value());

    World hostileHeight = w;
    hostileHeight.eroded.heightM[3] = 30000;
    CHECK(!gen(hostileHeight, {}).has_value());

    World hostileBoundary = w;
    hostileBoundary.tectonics.boundary[5] = 200;
    CHECK(!gen(hostileBoundary, {}).has_value());

    World empty;
    CHECK(!gen(empty, {}).has_value());
}

TEST_CASE("resources handle the smallest and a flat world", "[worldforge][resources][adversarial]")
{
    World w = makeWorld(7, kTectonicsMinSize, kTectonicsMinSize, kTectonicsMinPlates);
    const ResourceMap small = resources(w, { 1, 0, 64, 0 });
    CHECK(small.oreGrade.size() == static_cast<std::size_t>(kTectonicsMinSize) * kTectonicsMinSize);
    CHECK(small.leyNodes.size() <= 64);

    for (auto& h : w.eroded.heightM) h = static_cast<std::int16_t>(kTectonicsMaxHeightM);
    const ResourceMap peaks = resources(w, { 255, 0, 0, 0 });
    for (const auto a : peaks.arable) CHECK(a == 0);

    for (auto& h : w.eroded.heightM) h = static_cast<std::int16_t>(kTectonicsMinHeightM);
    const ResourceMap sea = resources(w, { 255, 0, 0, 0 });
    for (std::size_t c = 0; c < sea.oreGrade.size(); ++c) CHECK((sea.oreGrade[c] == 0 && sea.arable[c] == 0));
}
