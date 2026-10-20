#include <worldforge/poi_map.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
#include <utility>
#include <vector>

using namespace worldforge;

namespace {

constexpr int kSize = 96;

RoadMap flatRoad(int w, int h, std::int16_t level)
{
    RoadMap r;
    r.seed = 11;
    r.width = w;
    r.height = h;
    r.heightM.assign(static_cast<std::size_t>(w) * h, level);
    r.road.assign(static_cast<std::size_t>(w) * h, 0);
    return r;
}

void addPath(RoadMap& r, int x0, int y0, int x1, int y1)
{
    const std::uint32_t begin = static_cast<std::uint32_t>(r.pathCells.size());
    const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    for (int i = 0; i <= n; ++i) {
        const int x = x0 + (x1 - x0) * i / n;
        const int y = y0 + (y1 - y0) * i / n;
        r.road[r.index(x, y)] = 1;
        r.pathCells.push_back(static_cast<std::uint32_t>(r.index(x, y)));
    }
    r.edges.push_back({ 0, 1, false, n, begin, static_cast<std::uint32_t>(n + 1) });
}

RoadMap hillyRoad()
{
    RoadMap r = flatRoad(kSize, kSize, 0);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int lat = static_cast<int>(hashCell(x / 8, y / 8) % 90);
            const int rough = static_cast<int>(hashCell(x + 500, y + 500) % 9);
            r.heightM[r.index(x, y)] = static_cast<std::int16_t>(40 + lat + rough);
        }
    }
    addPath(r, 8, 20, 85, 30);
    addPath(r, 85, 30, 40, 80);
    return r;
}

PoiMap place(const RoadMap& r, const PoiParams& p = {}, const SettlementMap* s = nullptr)
{
    auto out = placePois(r, s, p);
    REQUIRE(out.has_value());
    return *out;
}

}

TEST_CASE("poi: placement is deterministic and matches the golden hash", "[worldforge][poi]")
{
    const RoadMap r = hillyRoad();
    const PoiMap a = place(r);
    const PoiMap b = place(r);
    CHECK(hashPoiMap(a) == hashPoiMap(b));
    CHECK_FALSE(a.pois.empty());
    CHECK(hashPoiMap(a) == 0x7c946c96de0ca832ull);
}

TEST_CASE("poi: a different seed breaks score ties differently", "[worldforge][poi]")
{
    RoadMap r = flatRoad(63, 63, 50);
    addPath(r, 10, 31, 52, 31);
    PoiParams p;
    p.maxPois = 1;
    std::set<std::pair<int, int>> firsts;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        r.seed = seed;
        const PoiMap m = place(r, p);
        REQUIRE(m.pois.size() == 1);
        firsts.insert({ m.pois[0].x, m.pois[0].y });
    }
    CHECK(firsts.size() > 1);
}

TEST_CASE("poi: placed POIs honour clearance, spacing, land and the score floor", "[worldforge][poi]")
{
    const RoadMap r = hillyRoad();
    PoiParams p;
    p.minScorePermille = 30;
    const PoiMap m = place(r, p);
    REQUIRE(m.pois.size() > 1);
    CHECK(m.pois.size() <= static_cast<std::size_t>(p.maxPois));
    for (std::size_t i = 0; i < m.pois.size(); ++i) {
        const Poi& a = m.pois[i];
        CHECK(a.score >= p.minScorePermille);
        CHECK(a.visibleCount > 0);
        CHECK(r.heightM[r.index(a.x, a.y)] > p.seaLevelM);
        for (int dy = -p.roadClearance; dy <= p.roadClearance; ++dy) {
            for (int dx = -p.roadClearance; dx <= p.roadClearance; ++dx) {
                const int x = a.x + dx;
                const int y = a.y + dy;
                if (x < 0 || y < 0 || x >= kSize || y >= kSize) continue;
                CHECK(r.road[r.index(x, y)] == 0);
            }
        }
        if (i > 0) CHECK(m.pois[i - 1].score >= a.score);
        for (std::size_t j = 0; j < i; ++j) {
            const int dx = m.pois[j].x - a.x;
            const int dy = m.pois[j].y - a.y;
            CHECK(dx * dx + dy * dy >= p.minSpacing * p.minSpacing);
        }
    }
}

TEST_CASE("poi: placed POIs are far better seen than random candidates", "[worldforge][poi]")
{
    const RoadMap r = hillyRoad();
    PoiParams p;
    p.maxPois = 8;
    const PoiMap m = place(r, p);
    REQUIRE(m.pois.size() == 8);
    const auto views = poiViewpoints(r, p.maxViewpoints);

    std::int64_t placedSum = 0;
    for (const Poi& q : m.pois) placedSum += poiSightline(r, views, p, q.x, q.y).scorePermille;
    const std::int64_t placedMean = placedSum / static_cast<std::int64_t>(m.pois.size());

    std::int64_t randomSum = 0;
    int randomN = 0;
    for (int i = 0; i < 400; ++i) {
        const int x = static_cast<int>(hashCell(i, 7) % kSize);
        const int y = static_cast<int>(hashCell(i, 9) % kSize);
        if (r.road[r.index(x, y)] != 0) continue;
        randomSum += poiSightline(r, views, p, x, y).scorePermille;
        ++randomN;
    }
    REQUIRE(randomN > 300);
    const std::int64_t randomMean = randomSum / randomN;
    CHECK(placedMean > 0);
    CHECK(placedMean >= randomMean * 3);
}

TEST_CASE("poi: terrain between the road and an entrance hides it", "[worldforge][poi]")
{
    RoadMap open = flatRoad(kSize, kSize, 50);
    addPath(open, 8, 48, 88, 48);
    RoadMap walled = open;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 50; x <= 52; ++x) {
            if (std::abs(y - 48) > 2) walled.heightM[walled.index(x, y)] = 400;
        }
    }
    PoiParams p;
    const auto views = poiViewpoints(open, p.maxViewpoints);
    const PoiSightline seen = poiSightline(open, views, p, 56, 20);
    const PoiSightline hidden = poiSightline(walled, views, p, 56, 20);
    CHECK(seen.scorePermille > 0);
    CHECK(hidden.scorePermille < seen.scorePermille);
    CHECK(hidden.visibleCount < seen.visibleCount);
}

TEST_CASE("poi: a POI hidden behind a wall is not chosen over an exposed one", "[worldforge][poi]")
{
    RoadMap r = flatRoad(kSize, kSize, 50);
    addPath(r, 8, 48, 88, 48);
    for (int x = 0; x < kSize; ++x) {
        for (int y = 52; y <= 54; ++y) r.heightM[r.index(x, y)] = 500;
    }
    PoiParams p;
    p.maxPois = 4;
    p.minSpacing = 8;
    const PoiMap m = place(r, p);
    REQUIRE_FALSE(m.pois.empty());
    for (const Poi& q : m.pois) CHECK(q.y < 52);
    CHECK(m.pois.front().score > 200);

    const auto views = poiViewpoints(r, p.maxViewpoints);
    CHECK(poiSightline(r, views, p, 40, 40).scorePermille > 0);
    CHECK(poiSightline(r, views, p, 40, 58).scorePermille == 0);
}

TEST_CASE("poi: settlement clearance keeps POIs off sites", "[worldforge][poi]")
{
    RoadMap r = flatRoad(kSize, kSize, 50);
    addPath(r, 8, 48, 88, 48);
    PoiParams p;
    p.maxPois = 64;
    p.minSpacing = 2;
    p.roadClearance = 0;
    const PoiMap before = place(r, p);
    REQUIRE_FALSE(before.pois.empty());

    SettlementMap s;
    s.seed = r.seed;
    s.width = r.width;
    s.height = r.height;
    for (const Poi& q : before.pois) s.sites.push_back({ q.x, q.y, 100, 0, 0 });
    const PoiMap after = place(r, p, &s);
    for (const Poi& q : after.pois) {
        for (const Settlement& site : s.sites) CHECK((std::abs(site.x - q.x) > p.settlementClearance || std::abs(site.y - q.y) > p.settlementClearance));
    }
}

TEST_CASE("poi: no roads, no land or zero budget places nothing", "[worldforge][poi]")
{
    CHECK(place(flatRoad(kSize, kSize, 50)).pois.empty());

    RoadMap ocean = flatRoad(kSize, kSize, -20);
    addPath(ocean, 8, 48, 88, 48);
    CHECK(place(ocean).pois.empty());

    RoadMap r = flatRoad(kSize, kSize, 50);
    addPath(r, 8, 48, 88, 48);
    PoiParams p;
    p.maxPois = 0;
    CHECK(place(r, p).pois.empty());
}

TEST_CASE("poi: viewpoints stay capped on a huge road network", "[worldforge][poi]")
{
    RoadMap r = flatRoad(1024, 1024, 50);
    for (int y = 4; y < 1024; y += 8) addPath(r, 2, y, 1021, y);
    PoiParams p;
    p.maxViewpoints = 64;
    p.maxCandidates = 256;
    const PoiMap m = place(r, p);
    CHECK(m.viewpointCount <= p.maxViewpoints);
    CHECK(m.viewpointCount > 0);
}

TEST_CASE("poi: malformed input and out-of-range parameters are rejected", "[worldforge][poi]")
{
    RoadMap r = flatRoad(kSize, kSize, 50);
    addPath(r, 8, 48, 88, 48);
    CHECK(placePois(r, nullptr).has_value());

    RoadMap tiny = flatRoad(4, 4, 50);
    CHECK_FALSE(placePois(tiny, nullptr).has_value());

    RoadMap shortHeights = r;
    shortHeights.heightM.pop_back();
    CHECK_FALSE(placePois(shortHeights, nullptr).has_value());

    RoadMap shortRoad = r;
    shortRoad.road.pop_back();
    CHECK_FALSE(placePois(shortRoad, nullptr).has_value());

    RoadMap hostileHeight = r;
    hostileHeight.heightM[100] = std::numeric_limits<std::int16_t>::max();
    CHECK_FALSE(placePois(hostileHeight, nullptr).has_value());

    RoadMap hostilePath = r;
    hostilePath.pathCells.push_back(static_cast<std::uint32_t>(r.heightM.size()));
    CHECK_FALSE(placePois(hostilePath, nullptr).has_value());
    hostilePath.pathCells.back() = std::numeric_limits<std::uint32_t>::max();
    CHECK_FALSE(placePois(hostilePath, nullptr).has_value());

    SettlementMap wrongGrid;
    wrongGrid.seed = r.seed;
    wrongGrid.width = 32;
    wrongGrid.height = 32;
    CHECK_FALSE(placePois(r, &wrongGrid).has_value());

    SettlementMap wrongSeed;
    wrongSeed.seed = r.seed + 1;
    wrongSeed.width = r.width;
    wrongSeed.height = r.height;
    CHECK_FALSE(placePois(r, &wrongSeed).has_value());

    SettlementMap offMap;
    offMap.seed = r.seed;
    offMap.width = r.width;
    offMap.height = r.height;
    offMap.sites.push_back({ 5000, 5, 100, 0, 0 });
    CHECK_FALSE(placePois(r, &offMap).has_value());

    const auto accepted = [&](auto mutate) {
        PoiParams p;
        mutate(p);
        return placePois(r, nullptr, p).has_value();
    };
    CHECK_FALSE(accepted([](PoiParams& p) { p.seaLevelM = std::numeric_limits<int>::max(); }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.maxPois = -1; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.maxPois = kPoiMaxPois + 1; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.minSpacing = 0; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.minSpacing = std::numeric_limits<int>::max(); }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.minScorePermille = 1001; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.viewRangeCells = 0; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.viewRangeCells = kPoiMaxViewRangeCells + 1; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.eyeHeightM = -1; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.entranceHeightM = 0; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.maxCandidates = 0; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.maxCandidates = kPoiMaxCandidates + 1; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.maxViewpoints = 0; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.maxViewpoints = kPoiMaxViewpoints + 1; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.roadClearance = -1; }));
    CHECK_FALSE(accepted([](PoiParams& p) { p.settlementClearance = kPoiMaxClearance + 1; }));
}

namespace {

RoadMap cornerRoad()
{
    RoadMap r = flatRoad(kSize, kSize, 50);
    addPath(r, 4, 6, 30, 12);
    return r;
}

PoiParams coverageParams(int reserve)
{
    PoiParams p;
    p.maxPois = 8;
    p.minSpacing = 12;
    p.viewRangeCells = 24;
    p.coverageReserve = reserve;
    p.coverage.viewRangeCells = 30;
    p.coverage.nearRangeCells = 6;
    return p;
}

int coverageOf(const RoadMap& r, const PoiMap& m, const PoiParams& p)
{
    std::vector<Landmark> marks;
    for (const Poi& poi : m.pois) marks.push_back({ poi.x, poi.y, p.coverageHeightM });
    const auto report = validateLandmarkCoverage({ r.width, r.height, r.heightM, marks }, p.coverage);
    REQUIRE(report.has_value());
    return report->coveragePermille;
}

}

TEST_CASE("poi: the coverage pass reaches land the road viewpoints cannot see", "[worldforge][poi]")
{
    const RoadMap r = cornerRoad();
    const PoiMap without = place(r, coverageParams(0));
    const PoiMap with = place(r, coverageParams(4));
    CHECK(coverageOf(r, with, coverageParams(4)) > coverageOf(r, without, coverageParams(0)) + 200);
    CHECK(coverageOf(r, with, coverageParams(4)) >= 900);
    CHECK(with.pois.size() <= 8);
}

TEST_CASE("poi: coverage POIs keep the clearances, the spacing and the land", "[worldforge][poi]")
{
    const RoadMap r = cornerRoad();
    const PoiParams p = coverageParams(4);
    const PoiMap m = place(r, p);
    for (std::size_t i = 0; i < m.pois.size(); ++i) {
        CHECK(r.heightM[r.index(m.pois[i].x, m.pois[i].y)] > 0);
        CHECK(r.road[r.index(m.pois[i].x, m.pois[i].y)] == 0);
        for (std::size_t j = i + 1; j < m.pois.size(); ++j) {
            const int dx = m.pois[i].x - m.pois[j].x;
            const int dy = m.pois[i].y - m.pois[j].y;
            CHECK(dx * dx + dy * dy >= p.minSpacing * p.minSpacing);
        }
    }
    const PoiMap again = place(r, p);
    CHECK(hashPoiMap(again) == hashPoiMap(m));
}

TEST_CASE("poi: the coverage pass places on land with no roads and no scored POIs", "[worldforge][poi]")
{
    RoadMap r = flatRoad(kSize, kSize, 50);
    CHECK(place(r, coverageParams(0)).pois.empty());
    const PoiMap m = place(r, coverageParams(4));
    CHECK_FALSE(m.pois.empty());
    CHECK(m.viewpointCount == 0);
}

TEST_CASE("poi: crowded land relaxes the clearances instead of staying uncovered", "[worldforge][poi]")
{
    RoadMap r = flatRoad(kSize, kSize, 50);
    SettlementMap s;
    s.seed = r.seed;
    s.width = r.width;
    s.height = r.height;
    for (int y = 4; y < kSize; y += 8) {
        for (int x = 4; x < kSize; x += 8) {
            Settlement site;
            site.x = x;
            site.y = y;
            s.sites.push_back(site);
        }
    }
    PoiParams p = coverageParams(4);
    p.minSpacing = 4;
    const PoiMap strict = place(r, coverageParams(0), &s);
    const PoiMap relaxed = place(r, p, &s);
    CHECK(strict.pois.empty());
    CHECK_FALSE(relaxed.pois.empty());
    for (const Poi& poi : relaxed.pois) {
        const bool nearSite = std::any_of(s.sites.begin(), s.sites.end(), [&](const Settlement& site) {
            return std::abs(site.x - poi.x) <= p.settlementClearance && std::abs(site.y - poi.y) <= p.settlementClearance;
        });
        CHECK(nearSite);
    }
}

TEST_CASE("poi: the coverage reserve is validated and the coverage POIs stay in the validator window", "[worldforge][poi]")
{
    RoadMap r = cornerRoad();
    PoiParams p = coverageParams(0);
    p.coverageReserve = p.maxPois + 1;
    CHECK_FALSE(placePois(r, nullptr, p).has_value());
    p = coverageParams(4);
    p.coverageReserve = -1;
    CHECK_FALSE(placePois(r, nullptr, p).has_value());
    p = coverageParams(4);
    p.coverageHeightM = 0;
    CHECK_FALSE(placePois(r, nullptr, p).has_value());
    p = coverageParams(4);
    p.coverage.viewRangeCells = 0;
    CHECK_FALSE(placePois(r, nullptr, p).has_value());

    PoiParams many;
    many.maxPois = kPoiMaxPois;
    many.minSpacing = 3;
    many.coverageReserve = 64;
    many.coverage.viewRangeCells = 10;
    many.coverage.nearRangeCells = 2;
    const PoiMap m = place(r, many);
    CHECK(m.pois.size() <= static_cast<std::size_t>(kPoiMaxPois));
    many.coverageReserve = kLandmarkMaxLandmarks + 1;
    CHECK_FALSE(placePois(r, nullptr, many).has_value());
}
