#include <worldforge/road_map.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <set>
#include <vector>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;

HydrologyMap blankMap(int w, int h, std::int16_t level)
{
    HydrologyMap m;
    m.seed = 7;
    m.width = w;
    m.height = h;
    const std::size_t cells = static_cast<std::size_t>(w) * h;
    m.heightM.assign(cells, level);
    m.filledM.assign(cells, level);
    m.lakeDepthM.assign(cells, 0);
    m.river.assign(cells, 0);
    return m;
}

// Rolling hills: bilinear lattice noise (cell 12) plus per-cell roughness.
HydrologyMap hills(int w, int h, std::uint64_t seed, int amplitude = 90)
{
    HydrologyMap m = blankMap(w, h, 0);
    const int step = 12;
    const auto lattice = [&](int lx, int ly) { return static_cast<int>(hashCell(lx, ly) ^ splitmix64(seed)) & 0x7fffffff; };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int lx = x / step;
            const int ly = y / step;
            const int fx = x % step;
            const int fy = y % step;
            const auto at = [&](int ox, int oy) { return static_cast<std::int64_t>(lattice(lx + ox, ly + oy) % (amplitude + 1)); };
            const std::int64_t top = at(0, 0) * (step - fx) + at(1, 0) * fx;
            const std::int64_t bot = at(0, 1) * (step - fx) + at(1, 1) * fx;
            const std::int64_t v = (top * (step - fy) + bot * fy) / (step * step);
            const int rough = static_cast<int>(hashCell(x + 1000, y + 1000) % 7) - 3;
            m.heightM[m.index(x, y)] = static_cast<std::int16_t>(50 + v + rough);
        }
    }
    m.filledM = m.heightM;
    return m;
}

std::vector<RoadSite> gridSites(const HydrologyMap& m, int cols, int rows)
{
    std::vector<RoadSite> sites;
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i) {
            const int x = (i * 2 + 1) * m.width / (cols * 2) + static_cast<int>(hashCell(i, j) % 5) - 2;
            const int y = (j * 2 + 1) * m.height / (rows * 2) + static_cast<int>(hashCell(j, i + 50) % 5) - 2;
            sites.push_back({ x, y });
        }
    }
    return sites;
}

RoadMap roads(const HydrologyMap& m, const std::vector<RoadSite>& sites, const RoadParams& p = {}, const std::vector<std::uint8_t>& biome = {})
{
    auto out = computeRoads(m, sites, p, biome);
    REQUIRE(out.has_value());
    return *out;
}

bool siteConnectedViaRoads(const RoadMap& r, const std::vector<RoadSite>& sites)
{
    std::vector<std::uint8_t> seen(r.road.size(), 0);
    std::vector<std::size_t> stack{ r.index(sites[0].x, sites[0].y) };
    seen[stack[0]] = 1;
    while (!stack.empty()) {
        const std::size_t c = stack.back();
        stack.pop_back();
        const int cx = static_cast<int>(c % r.width);
        const int cy = static_cast<int>(c / r.width);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = cx + dx;
                const int ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= r.width || ny >= r.height) continue;
                const std::size_t n = r.index(nx, ny);
                if (r.road[n] == 0 || seen[n]) continue;
                seen[n] = 1;
                stack.push_back(n);
            }
        }
    }
    for (const RoadSite& s : sites) {
        if (!seen[r.index(s.x, s.y)]) return false;
    }
    return true;
}

// Sum of |dz| and step count along a cell sequence.
struct SlopeSum {
    std::int64_t dz = 0;
    std::int64_t steps = 0;
    double mean() const { return steps == 0 ? 0.0 : static_cast<double>(dz) / static_cast<double>(steps); }
};

SlopeSum pathSlope(const std::vector<std::int16_t>& z, const std::vector<std::uint32_t>& cells, std::uint32_t begin, std::uint32_t count)
{
    SlopeSum s;
    for (std::uint32_t i = 1; i < count; ++i) {
        s.dz += std::abs(static_cast<int>(z[cells[begin + i]]) - static_cast<int>(z[cells[begin + i - 1]]));
        ++s.steps;
    }
    return s;
}

std::vector<std::uint32_t> straightLine(const HydrologyMap& m, RoadSite a, RoadSite b)
{
    std::vector<std::uint32_t> line;
    const int n = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
    for (int i = 0; i <= n; ++i) {
        const int x = n == 0 ? a.x : a.x + (b.x - a.x) * i / n;
        const int y = n == 0 ? a.y : a.y + (b.y - a.y) * i / n;
        line.push_back(static_cast<std::uint32_t>(m.index(x, y)));
    }
    return line;
}

HydrologyMap worldHydrology()
{
    auto base = generateTectonics(kSeed, { 128, 96, 12 });
    REQUIRE(base.has_value());
    auto eroded = erodeTectonics(*base);
    REQUIRE(eroded.has_value());
    auto hydro = computeHydrology(*eroded);
    REQUIRE(hydro.has_value());
    return *hydro;
}

std::vector<RoadSite> landSites(const HydrologyMap& m, int stride)
{
    std::vector<RoadSite> sites;
    for (int y = stride / 2; y < m.height; y += stride) {
        for (int x = stride / 2; x < m.width; x += stride) {
            const std::size_t c = m.index(x, y);
            if (m.heightM[c] > 20 && m.lakeDepthM[c] == 0) sites.push_back({ x, y });
        }
    }
    return sites;
}

}

TEST_CASE("roads connect every site on one land mass", "[worldforge][roads]")
{
    const HydrologyMap m = hills(120, 90, 3);
    const auto sites = gridSites(m, 5, 4);
    const RoadMap r = roads(m, sites);
    CHECK(r.components == 1);
    CHECK(siteConnectedViaRoads(r, sites));
    CHECK(r.edges.size() >= sites.size() - 1);
}

TEST_CASE("road routes have lower mean slope than straight lines between the same sites", "[worldforge][roads]")
{
    const HydrologyMap m = hills(120, 90, 4);
    const auto sites = gridSites(m, 5, 4);
    const RoadMap r = roads(m, sites);
    SlopeSum routed;
    SlopeSum straight;
    for (const RoadEdge& e : r.edges) {
        const SlopeSum a = pathSlope(m.heightM, r.pathCells, e.pathBegin, e.pathCount);
        const auto line = straightLine(m, sites[e.a], sites[e.b]);
        const SlopeSum b = pathSlope(m.heightM, line, 0, static_cast<std::uint32_t>(line.size()));
        routed.dz += a.dz;
        routed.steps += a.steps;
        straight.dz += b.dz;
        straight.steps += b.steps;
    }
    REQUIRE(routed.steps > 0);
    CHECK(routed.mean() < 0.8 * straight.mean());

    RoadParams blind;
    blind.slopeWeight = 0;
    const RoadMap flat = roads(m, sites, blind);
    SlopeSum blindSum;
    for (const RoadEdge& e : flat.edges) {
        const SlopeSum a = pathSlope(m.heightM, flat.pathCells, e.pathBegin, e.pathCount);
        blindSum.dz += a.dz;
        blindSum.steps += a.steps;
    }
    CHECK(routed.mean() < blindSum.mean());
}

TEST_CASE("deformation reduces slope along the road", "[worldforge][roads]")
{
    const HydrologyMap m = hills(120, 90, 5);
    const auto sites = gridSites(m, 4, 3);
    const RoadMap r = roads(m, sites);
    RoadParams off;
    off.smoothRadius = 0;
    const RoadMap undeformed = roads(m, sites, off);

    SlopeSum before;
    SlopeSum after;
    for (const RoadEdge& e : r.edges) {
        const SlopeSum a = pathSlope(m.heightM, r.pathCells, e.pathBegin, e.pathCount);
        const SlopeSum b = pathSlope(r.heightM, r.pathCells, e.pathBegin, e.pathCount);
        before.dz += a.dz;
        before.steps += a.steps;
        after.dz += b.dz;
        after.steps += b.steps;
    }
    REQUIRE(before.steps > 0);
    CHECK(after.mean() < 0.8 * before.mean());
    CHECK(undeformed.heightM == m.heightM);
    CHECK(r.heightM != m.heightM);
}

TEST_CASE("only the road and its shoulders are deformed", "[worldforge][roads]")
{
    const HydrologyMap m = hills(120, 90, 6);
    const auto sites = gridSites(m, 3, 3);
    const RoadMap r = roads(m, sites);
    int changedRoad = 0;
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            bool nearRoad = false;
            for (int dy = -1; dy <= 1 && !nearRoad; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < m.width && ny < m.height && r.road[r.index(nx, ny)] != 0) nearRoad = true;
                }
            }
            const std::size_t c = m.index(x, y);
            if (!nearRoad) CHECK(r.heightM[c] == m.heightM[c]);
            else if (r.road[c] != 0 && r.heightM[c] != m.heightM[c]) ++changedRoad;
        }
    }
    CHECK(changedRoad > 0);
}

TEST_CASE("the road takes the pass through a ridge", "[worldforge][roads]")
{
    HydrologyMap m = blankMap(96, 64, 100);
    for (int y = 0; y < 64; ++y) {
        for (int x = 44; x < 52; ++x) m.heightM[m.index(x, y)] = y >= 48 && y < 54 ? 110 : 600;
    }
    m.filledM = m.heightM;
    const std::vector<RoadSite> sites{ { 10, 10 }, { 86, 10 } };
    RoadParams p;
    p.smoothRadius = 0;
    const RoadMap r = roads(m, sites, p);
    REQUIRE(r.edges.size() == 1);
    for (std::uint32_t i = 0; i < r.edges[0].pathCount; ++i) CHECK(m.heightM[r.pathCells[r.edges[0].pathBegin + i]] < 200);
}

TEST_CASE("a river is crossed once and marked as a crossing", "[worldforge][roads]")
{
    HydrologyMap m = blankMap(96, 64, 100);
    for (int y = 0; y < 64; ++y) m.river[m.index(40, y)] = 1;
    const std::vector<RoadSite> sites{ { 10, 30 }, { 80, 30 } };
    const RoadMap r = roads(m, sites);
    int crossings = 0;
    for (const std::uint8_t v : r.road) crossings += v == 2 ? 1 : 0;
    CHECK(crossings == 1);
    CHECK(r.components == 1);
}

TEST_CASE("roads never enter ocean or lakes and separate land masses stay apart", "[worldforge][roads]")
{
    HydrologyMap m = hills(96, 64, 8);
    for (int y = 0; y < 64; ++y) {
        for (int x = 46; x < 50; ++x) m.heightM[m.index(x, y)] = -10;
    }
    for (int y = 20; y < 30; ++y) {
        for (int x = 10; x < 20; ++x) m.lakeDepthM[m.index(x, y)] = 5;
    }
    m.filledM = m.heightM;
    const std::vector<RoadSite> sites{ { 5, 5 }, { 30, 40 }, { 70, 10 }, { 90, 50 } };
    const RoadMap r = roads(m, sites);
    CHECK(r.components == 2);
    for (std::size_t c = 0; c < r.road.size(); ++c) {
        if (r.road[c] != 0) {
            CHECK(m.heightM[c] > 0);
            CHECK(m.lakeDepthM[c] == 0);
        }
    }
    for (const RoadEdge& e : r.edges) CHECK((sites[e.a].x < 46) == (sites[e.b].x < 46));
}

TEST_CASE("biome cost steers routes and 255 blocks them", "[worldforge][roads]")
{
    const HydrologyMap m = blankMap(96, 64, 100);
    const std::vector<RoadSite> sites{ { 10, 32 }, { 86, 32 } };
    std::vector<std::uint8_t> biome(m.heightM.size(), 0);
    for (int y = 0; y < 64; ++y) {
        for (int x = 40; x < 56; ++x) biome[m.index(x, y)] = (y >= 8 && y < 14) ? 0 : 200;
    }
    const RoadMap detour = roads(m, sites, {}, biome);
    bool throughGap = true;
    for (std::uint32_t i = 0; i < detour.edges[0].pathCount; ++i) {
        const std::size_t c = detour.pathCells[i];
        if (c % 96 >= 40 && c % 96 < 56) throughGap = throughGap && c / 96 >= 8 && c / 96 < 14;
    }
    CHECK(throughGap);

    const RoadMap direct = roads(m, sites);
    const auto costly = [&](const RoadMap& r) {
        int n = 0;
        for (std::uint32_t i = 0; i < r.edges[0].pathCount; ++i) n += biome[r.pathCells[i]] != 0 ? 1 : 0;
        return n;
    };
    CHECK(costly(direct) > 0);
    CHECK(costly(detour) == 0);

    for (int y = 0; y < 64; ++y) {
        for (int x = 40; x < 56; ++x) biome[m.index(x, y)] = kRoadsImpassable;
    }
    CHECK(roads(m, sites, {}, biome).components == 2);
}

TEST_CASE("extra edges add about 15 percent loops to the tree", "[worldforge][roads]")
{
    const HydrologyMap m = hills(200, 150, 9);
    const auto sites = gridSites(m, 7, 6);
    const RoadMap r = roads(m, sites);
    std::size_t tree = 0;
    std::size_t extra = 0;
    for (const RoadEdge& e : r.edges) (e.extra ? extra : tree)++;
    CHECK(r.components == 1);
    CHECK(tree == sites.size() - 1);
    CHECK(extra == (tree * 150 + 500) / 1000);
    CHECK(extra > 0);

    RoadParams none;
    none.extraEdgePermille = 0;
    CHECK(roads(m, sites, none).edges.size() == sites.size() - 1);
}

TEST_CASE("one site and two sites degrade gracefully", "[worldforge][roads]")
{
    const HydrologyMap m = hills(64, 64, 10);
    const RoadMap one = roads(m, { { 10, 10 } });
    CHECK(one.edges.empty());
    CHECK(one.components == 1);
    const RoadMap two = roads(m, { { 10, 10 }, { 50, 50 } });
    CHECK(two.edges.size() == 1);
    CHECK(two.edges[0].a == 0);
    CHECK(two.edges[0].b == 1);
    CHECK(two.edges[0].extra == false);
}

TEST_CASE("route paths are contiguous and start and end on their sites", "[worldforge][roads]")
{
    const HydrologyMap m = hills(100, 80, 11);
    const auto sites = gridSites(m, 4, 3);
    const RoadMap r = roads(m, sites);
    for (const RoadEdge& e : r.edges) {
        REQUIRE(e.pathCount >= 2);
        CHECK(r.pathCells[e.pathBegin] == m.index(sites[e.a].x, sites[e.a].y));
        CHECK(r.pathCells[e.pathBegin + e.pathCount - 1] == m.index(sites[e.b].x, sites[e.b].y));
        for (std::uint32_t i = 1; i < e.pathCount; ++i) {
            const int a = static_cast<int>(r.pathCells[e.pathBegin + i - 1]);
            const int b = static_cast<int>(r.pathCells[e.pathBegin + i]);
            CHECK(std::abs(a % m.width - b % m.width) <= 1);
            CHECK(std::abs(a / m.width - b / m.width) <= 1);
            CHECK(a != b);
        }
    }
}

TEST_CASE("roads on generated world terrain stay valid", "[worldforge][roads]")
{
    const HydrologyMap m = worldHydrology();
    const auto sites = landSites(m, 14);
    REQUIRE(sites.size() >= 6);
    const RoadMap r = roads(m, sites);
    CHECK(r.components >= 1);
    for (std::size_t c = 0; c < r.road.size(); ++c) {
        if (r.road[c] != 0) {
            CHECK(m.heightM[c] > 0);
            CHECK(m.lakeDepthM[c] == 0);
        }
        CHECK(r.heightM[c] >= (m.heightM[c] > 0 ? 1 : m.heightM[c]));
    }
}

TEST_CASE("roads are deterministic across repeat runs", "[worldforge][roads]")
{
    const HydrologyMap m = hills(120, 90, 12);
    const auto sites = gridSites(m, 5, 4);
    const RoadMap a = roads(m, sites);
    const RoadMap b = roads(m, sites);
    CHECK(a.heightM == b.heightM);
    CHECK(a.pathCells == b.pathCells);
    CHECK(hashRoadMap(a) == hashRoadMap(b));
}

TEST_CASE("roads golden hash is stable", "[worldforge][roads]")
{
    const HydrologyMap m = worldHydrology();
    CHECK(hashRoadMap(roads(m, landSites(m, 14))) == 0x50886001ccfec784ull);
}

TEST_CASE("different terrain gives different roads", "[worldforge][roads]")
{
    std::set<std::uint64_t> hashes;
    for (std::uint64_t s = 0; s < 6; ++s) {
        const HydrologyMap m = hills(100, 80, 20 + s);
        hashes.insert(hashRoadMap(roads(m, gridSites(m, 4, 3))));
    }
    CHECK(hashes.size() == 6);

    const HydrologyMap a = hills(100, 80, 31);
    const HydrologyMap b = hills(100, 80, 32);
    CHECK(roads(a, gridSites(a, 4, 3)).pathCells != roads(b, gridSites(b, 4, 3)).pathCells);
}

TEST_CASE("hostile sites and malformed input are rejected", "[worldforge][roads]")
{
    const HydrologyMap m = hills(64, 64, 13);
    const auto ok = [&](const std::vector<RoadSite>& s, const RoadParams& p = {}, const std::vector<std::uint8_t>& b = {}) { return computeRoads(m, s, p, b).has_value(); };
    CHECK(ok({ { 5, 5 }, { 40, 40 } }));
    CHECK_FALSE(ok({}));
    CHECK_FALSE(ok({ { 5, 5 }, { 5, 5 } }));
    CHECK_FALSE(ok({ { -1, 5 } }));
    CHECK_FALSE(ok({ { 5, 64 } }));
    CHECK_FALSE(ok({ { 5, 5 }, { 2147483647, 5 } }));

    std::vector<RoadSite> many;
    for (int i = 0; i < kRoadsMaxSites + 1; ++i) many.push_back({ i % 60, i / 60 });
    CHECK_FALSE(ok(many));

    CHECK_FALSE(ok({ { 5, 5 } }, {}, std::vector<std::uint8_t>(10, 0)));
    std::vector<std::uint8_t> blocked(m.heightM.size(), 0);
    blocked[m.index(5, 5)] = kRoadsImpassable;
    CHECK_FALSE(ok({ { 5, 5 } }, {}, blocked));

    HydrologyMap sea = m;
    sea.heightM[sea.index(5, 5)] = -5;
    CHECK_FALSE(computeRoads(sea, { { 5, 5 } }).has_value());
    HydrologyMap lake = m;
    lake.lakeDepthM[lake.index(5, 5)] = 2;
    CHECK_FALSE(computeRoads(lake, { { 5, 5 } }).has_value());

    const std::vector<RoadSite> two{ { 5, 5 }, { 40, 40 } };
    const auto bad = [&](auto mutate) {
        RoadParams p;
        mutate(p);
        return ok(two, p);
    };
    CHECK_FALSE(bad([](RoadParams& p) { p.cellSizeM = 0; }));
    CHECK_FALSE(bad([](RoadParams& p) { p.cellSizeM = kRoadsMaxCellSizeM + 1; }));
    CHECK_FALSE(bad([](RoadParams& p) { p.slopeWeight = -1; }));
    CHECK_FALSE(bad([](RoadParams& p) { p.slopeWeight = kRoadsMaxSlopeWeight + 1; }));
    CHECK_FALSE(bad([](RoadParams& p) { p.riverCost = kRoadsMaxRiverCost + 1; }));
    CHECK_FALSE(bad([](RoadParams& p) { p.biomeWeight = -1; }));
    CHECK_FALSE(bad([](RoadParams& p) { p.extraEdgePermille = kRoadsMaxExtraPermille + 1; }));
    CHECK_FALSE(bad([](RoadParams& p) { p.candidateNeighbours = 0; }));
    CHECK_FALSE(bad([](RoadParams& p) { p.smoothRadius = -1; }));
    CHECK_FALSE(bad([](RoadParams& p) { p.smoothRadius = kRoadsMaxSmoothRadius + 1; }));
    CHECK_FALSE(bad([](RoadParams& p) { p.seaLevelM = kTectonicsMinHeightM - 1; }));

    HydrologyMap shortField = m;
    shortField.river.pop_back();
    CHECK_FALSE(computeRoads(shortField, two).has_value());
    HydrologyMap tiny = blankMap(4, 4, 100);
    CHECK_FALSE(computeRoads(tiny, { { 1, 1 } }).has_value());
    HydrologyMap tall = m;
    tall.heightM[3] = 9000;
    CHECK_FALSE(computeRoads(tall, two).has_value());
}

TEST_CASE("extreme terrain stays inside the height range", "[worldforge][roads]")
{
    HydrologyMap m = blankMap(64, 64, 100);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) m.heightM[m.index(x, y)] = ((x * 7 + y * 13 + x * y) % 3) == 0 ? kTectonicsMaxHeightM : ((x + y) % 3 == 1 ? 1 : 4000);
    }
    m.filledM = m.heightM;
    RoadParams p;
    p.cellSizeM = 1;
    p.slopeWeight = kRoadsMaxSlopeWeight;
    p.smoothRadius = kRoadsMaxSmoothRadius;
    const RoadMap r = roads(m, { { 2, 2 }, { 60, 60 }, { 2, 60 } }, p);
    for (const std::int16_t v : r.heightM) CHECK((v >= 1 && v <= kTectonicsMaxHeightM));
}

TEST_CASE("a large map with many sites finishes with bounded output", "[worldforge][roads]")
{
    const HydrologyMap m = hills(256, 256, 14);
    const auto sites = gridSites(m, 10, 10);
    const RoadMap r = roads(m, sites);
    CHECK(r.components == 1);
    CHECK(r.pathCells.size() <= kRoadsMaxPathCells);
    CHECK(siteConnectedViaRoads(r, sites));
}
