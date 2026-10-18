#include <worldforge/politics_map.h>

#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;

struct World {
    ErodedMap eroded;
    ResourceMap resources;
    std::vector<PoliticsSite> sites;
};

World makeWorld(std::uint64_t seed, int w, int h, int plates, int stride)
{
    auto t = generateTectonics(seed, { w, h, plates });
    REQUIRE(t.has_value());
    auto e = erodeTectonics(*t);
    REQUIRE(e.has_value());
    auto r = generateResources({ &*t, &*e, {} });
    REQUIRE(r.has_value());
    World world{ *e, *r, {} };
    for (int y = stride / 2; y < h; y += stride) {
        for (int x = stride / 2; x < w; x += stride) {
            if (world.eroded.heightM[world.eroded.index(x, y)] >= 0) world.sites.push_back({ static_cast<std::int16_t>(x), static_cast<std::int16_t>(y) });
        }
    }
    return world;
}

// Flat land (or ocean where `sea` says so) with no ore and uniform arable, for exact expectations.
World flatWorld(int w, int h)
{
    World world;
    world.eroded.seed = 9;
    world.eroded.width = w;
    world.eroded.height = h;
    world.eroded.heightM.assign(static_cast<std::size_t>(w) * h, 100);
    world.resources.seed = 9;
    world.resources.width = w;
    world.resources.height = h;
    world.resources.oreKind.assign(static_cast<std::size_t>(w) * h, 0);
    world.resources.oreGrade.assign(static_cast<std::size_t>(w) * h, 0);
    world.resources.arable.assign(static_cast<std::size_t>(w) * h, 128);
    return world;
}

std::vector<FactionSpec> ecology(std::size_t count)
{
    std::vector<FactionSpec> f(count);
    if (count > 0) f[0] = { ClaimStrategy::Contiguous, -1, 0, 2, 0, 0, 0, 0, 0 };
    if (count > 1) f[1] = { ClaimStrategy::ResourceFollowing, -1, 0, 0, 3, 0, 0, 40, -60 };
    if (count > 2) f[2] = { ClaimStrategy::Defensive, -1, 0, 0, 0, 0, 3, -80, 70 };
    if (count > 3) f[3] = { ClaimStrategy::Contiguous, -1, 0, 0, 0, 3, 0, 100, 20 };
    if (count > 4) f[4] = { ClaimStrategy::NodesOnly, -1, 0, 0, 0, 2, 0, -20, 0 };
    return f;
}

PoliticsMap politics(const World& w, std::span<const FactionSpec> factions, const PoliticsParams& p = {})
{
    auto m = generatePolitics({ &w.eroded, &w.resources, w.sites, factions }, p);
    REQUIRE(m.has_value());
    return *m;
}

bool isLand(const World& w, std::size_t c)
{
    return w.eroded.heightM[c] >= 0;
}

// Land cells reachable from any claiming faction's home over 4-connected land.
std::vector<std::uint8_t> reachableFromHomes(const World& w, const PoliticsMap& m, std::span<const FactionSpec> factions)
{
    std::vector<std::uint8_t> seen(w.eroded.heightM.size(), 0);
    std::vector<std::size_t> queue;
    for (std::size_t f = 0; f < factions.size(); ++f) {
        if (factions[f].strategy == ClaimStrategy::Routes) continue;
        const std::size_t c = m.index(m.homes[f].x, m.homes[f].y);
        seen[c] = 1;
        queue.push_back(c);
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int x = static_cast<int>(queue[head] % w.eroded.width);
        const int y = static_cast<int>(queue[head] / w.eroded.width);
        const int dx[4] = { 1, -1, 0, 0 };
        const int dy[4] = { 0, 0, 1, -1 };
        for (int n = 0; n < 4; ++n) {
            const int nx = x + dx[n];
            const int ny = y + dy[n];
            if (nx < 0 || ny < 0 || nx >= w.eroded.width || ny >= w.eroded.height) continue;
            const std::size_t nc = m.index(nx, ny);
            if (seen[nc] != 0 || !isLand(w, nc)) continue;
            seen[nc] = 1;
            queue.push_back(nc);
        }
    }
    return seen;
}

// Manhattan distance to the nearest 4-neighbour cell boundary between two different owners, capped.
std::vector<int> borderDistance(const PoliticsMap& m, int cap)
{
    std::vector<int> dist(m.owner.size(), cap);
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            const std::size_t c = m.index(x, y);
            if (m.owner[c] == kPoliticsNoOwner) continue;
            for (int r = 0; r < cap; ++r) {
                bool found = false;
                for (int dy = -r; dy <= r && !found; ++dy) {
                    for (int dx = -r; dx <= r && !found; ++dx) {
                        if (std::abs(dx) + std::abs(dy) != r) continue;
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= m.width || ny >= m.height) continue;
                        const std::uint8_t o = m.owner[m.index(nx, ny)];
                        if (o != kPoliticsNoOwner && o != m.owner[c]) found = true;
                    }
                }
                if (found) {
                    dist[c] = r;
                    break;
                }
            }
        }
    }
    return dist;
}

std::size_t contestedCount(const PoliticsMap& m)
{
    std::size_t n = 0;
    for (std::size_t c = 0; c < m.owner.size(); ++c) n += m.contested(c) ? 1 : 0;
    return n;
}

}

TEST_CASE("politics are deterministic and match the golden hash", "[worldforge][politics]")
{
    const World w = makeWorld(kSeed, 128, 128, 10, 24);
    const auto factions = ecology(5);
    const PoliticsMap a = politics(w, factions);
    const PoliticsMap b = politics(w, factions);
    CHECK(hashPoliticsMap(a) == hashPoliticsMap(b));
    CHECK(a.owner == b.owner);
    CHECK(hashPoliticsMap(a) == 0x1218edc5de58c67eull);
}

TEST_CASE("different seeds give different politics", "[worldforge][politics]")
{
    const World a = makeWorld(1, 96, 96, 8, 20);
    const World b = makeWorld(2, 96, 96, 8, 20);
    const auto factions = ecology(4);
    CHECK(hashPoliticsMap(politics(a, factions)) != hashPoliticsMap(politics(b, factions)));
}

TEST_CASE("every land cell of a populated landmass is owned", "[worldforge][politics]")
{
    const World w = makeWorld(kSeed, 192, 192, 14, 32);
    const auto factions = ecology(4);
    const PoliticsMap m = politics(w, factions);
    const std::vector<std::uint8_t> reach = reachableFromHomes(w, m, factions);
    std::size_t reachable = 0, owned = 0;
    for (std::size_t c = 0; c < m.owner.size(); ++c) {
        if (!isLand(w, c)) {
            CHECK(m.owner[c] == kPoliticsNoOwner);
            continue;
        }
        if (reach[c] == 0) continue;
        ++reachable;
        if (m.owner[c] != kPoliticsNoOwner) ++owned;
    }
    REQUIRE(reachable > 1000);
    CHECK(owned == reachable);
    std::uint32_t total = 0;
    for (const std::uint32_t t : m.territory) total += t;
    CHECK(total == owned);
    for (const std::uint32_t t : m.territory) CHECK(t > 0);
}

TEST_CASE("every territory is 4-connected to its home", "[worldforge][politics]")
{
    const World w = makeWorld(kSeed, 192, 192, 14, 32);
    const auto factions = ecology(5);
    const PoliticsMap m = politics(w, factions);
    for (std::size_t f = 0; f < factions.size(); ++f) {
        std::vector<std::uint8_t> seen(m.owner.size(), 0);
        std::vector<std::size_t> queue{ m.index(m.homes[f].x, m.homes[f].y) };
        REQUIRE(m.owner[queue[0]] == f);
        seen[queue[0]] = 1;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const int x = static_cast<int>(queue[head] % m.width);
            const int y = static_cast<int>(queue[head] / m.width);
            const int dx[4] = { 1, -1, 0, 0 };
            const int dy[4] = { 0, 0, 1, -1 };
            for (int n = 0; n < 4; ++n) {
                const int nx = x + dx[n];
                const int ny = y + dy[n];
                if (nx < 0 || ny < 0 || nx >= m.width || ny >= m.height) continue;
                const std::size_t nc = m.index(nx, ny);
                if (seen[nc] != 0 || m.owner[nc] != f) continue;
                seen[nc] = 1;
                queue.push_back(nc);
            }
        }
        CHECK(queue.size() == m.territory[f]);
    }
}

TEST_CASE("contested cells lie on borders and nowhere else", "[worldforge][politics]")
{
    const World w = makeWorld(kSeed, 192, 192, 14, 32);
    const auto factions = ecology(5);
    const PoliticsMap m = politics(w, factions, { 24, 6, 8 });
    const std::vector<int> dist = borderDistance(m, 8);
    std::size_t nearTotal = 0, nearContested = 0, farTotal = 0, farContested = 0;
    for (std::size_t c = 0; c < m.owner.size(); ++c) {
        if (m.owner[c] == kPoliticsNoOwner) continue;
        if (m.contested(c)) {
            CHECK(dist[c] <= 1);
            CHECK(m.rival[c] != m.owner[c]);
        }
        if (dist[c] <= 1) {
            ++nearTotal;
            if (m.contested(c)) ++nearContested;
        } else if (dist[c] >= 4) {
            ++farTotal;
            if (m.contested(c)) ++farContested;
        }
    }
    REQUIRE(nearTotal > 0);
    REQUIRE(farTotal > 0);
    CHECK(nearContested > nearTotal / 10);
    CHECK(farContested == 0);
}

TEST_CASE("the contest margin widens or removes contested ground", "[worldforge][politics]")
{
    const World w = makeWorld(kSeed, 128, 128, 10, 24);
    const auto factions = ecology(4);
    const std::size_t none = contestedCount(politics(w, factions, { 0, 6, 8 }));
    const std::size_t some = contestedCount(politics(w, factions, { 24, 6, 8 }));
    const std::size_t lots = contestedCount(politics(w, factions, { 400, 6, 8 }));
    CHECK(some > none);
    CHECK(lots > some);
}

TEST_CASE("two factions on flat land split it at the midline", "[worldforge][politics]")
{
    World w = flatWorld(32, 32);
    w.sites = { { 4, 16 }, { 27, 16 } };
    const std::vector<FactionSpec> factions(2);
    const PoliticsMap m = politics(w, factions, { 0, 6, 8 });
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const std::uint8_t o = m.owner[m.index(x, y)];
            if (x <= 15) CHECK(o == 0);
            else CHECK(o == 1);
        }
    }
    CHECK(m.territory[0] == 16u * 32u);
    CHECK(m.territory[1] == 16u * 32u);
}

TEST_CASE("an exact cost tie goes to the lower faction index", "[worldforge][politics]")
{
    World w = flatWorld(17, 17);
    w.sites = { { 2, 8 }, { 14, 8 } };
    const std::vector<FactionSpec> factions(2);
    const PoliticsMap m = politics(w, factions);
    CHECK(m.owner[m.index(8, 8)] == 0);
    CHECK(m.owner[m.index(8, 0)] == 0);
}

TEST_CASE("a landmass with no home stays unowned", "[worldforge][politics]")
{
    World w = flatWorld(32, 32);
    for (int y = 0; y < 32; ++y) w.eroded.heightM[w.eroded.index(16, y)] = -50;
    w.sites = { { 4, 4 } };
    const std::vector<FactionSpec> factions(1);
    const PoliticsMap m = politics(w, factions);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const std::uint8_t o = m.owner[m.index(x, y)];
            if (x < 16) CHECK(o == 0);
            else CHECK(o == kPoliticsNoOwner);
        }
    }
}

TEST_CASE("NodesOnly stays inside its radius and Routes claims nothing", "[worldforge][politics]")
{
    World w = flatWorld(48, 48);
    w.sites = { { 10, 10 }, { 36, 36 }, { 24, 24 } };
    std::vector<FactionSpec> factions(3);
    factions[0].strategy = ClaimStrategy::NodesOnly;
    factions[1].strategy = ClaimStrategy::Routes;
    const PoliticsMap m = politics(w, factions, { 24, 5, 8 });
    CHECK(m.territory[1] == 0);
    CHECK(m.owner[m.index(36, 36)] != 1);
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            if (m.owner[m.index(x, y)] == 0) CHECK(std::abs(x - 10) + std::abs(y - 10) <= 5);
        }
    }
    CHECK(m.territory[0] > 1);
    CHECK(m.territory[2] > m.territory[0]);
}

TEST_CASE("a resource-following faction takes more of the ore than a contiguous one", "[worldforge][politics]")
{
    World w = flatWorld(64, 64);
    w.sites = { { 8, 32 }, { 55, 32 } };
    for (int y = 0; y < 64; ++y) {
        for (int x = 28; x < 60; ++x) {
            const std::size_t c = w.eroded.index(x, y);
            if (y % 8 == 0 || x % 8 == 0) w.resources.oreGrade[c] = 200;
        }
    }
    std::vector<FactionSpec> factions(2);
    const auto oreOwned = [&](const PoliticsMap& m) {
        int n = 0;
        for (std::size_t c = 0; c < m.owner.size(); ++c) n += (w.resources.oreGrade[c] > 0 && m.owner[c] == 0) ? 1 : 0;
        return n;
    };
    const int plain = oreOwned(politics(w, factions));
    factions[0].strategy = ClaimStrategy::ResourceFollowing;
    const int following = oreOwned(politics(w, factions));
    CHECK(following > plain + 20);
}

TEST_CASE("a defensive faction prefers marginal land over farmland", "[worldforge][politics]")
{
    World w = flatWorld(64, 32);
    w.sites = { { 8, 16 }, { 55, 16 } };
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 64; ++x) w.resources.arable[w.eroded.index(x, y)] = x < 32 ? 255 : 0;
    }
    std::vector<FactionSpec> factions(2);
    const std::uint32_t plain = politics(w, factions).territory[1];
    factions[1].strategy = ClaimStrategy::Defensive;
    const std::uint32_t defensive = politics(w, factions).territory[1];
    CHECK(defensive > plain + 40);
}

TEST_CASE("decay with distance shrinks a faction", "[worldforge][politics]")
{
    World w = flatWorld(64, 32);
    w.sites = { { 8, 16 }, { 55, 16 } };
    std::vector<FactionSpec> factions(2);
    const std::uint32_t plain = politics(w, factions).territory[0];
    factions[0].decayPerCell = 32;
    const std::uint32_t decayed = politics(w, factions).territory[0];
    CHECK(decayed + 40 < plain);
}

TEST_CASE("reach bounds a faction and leaves the rest unowned", "[worldforge][politics]")
{
    World w = flatWorld(48, 48);
    w.sites = { { 24, 24 } };
    std::vector<FactionSpec> factions(1);
    factions[0].reach = 7;
    const PoliticsMap m = politics(w, factions);
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            const bool inside = std::abs(x - 24) + std::abs(y - 24) <= 7;
            CHECK((m.owner[m.index(x, y)] == 0) == inside);
        }
    }
}

TEST_CASE("hostility is ideological distance plus resource overlap", "[worldforge][politics]")
{
    World w = flatWorld(32, 32);
    w.sites = { { 4, 16 }, { 27, 16 }, { 16, 2 } };
    std::vector<FactionSpec> factions(3);
    factions[0].oldOrder = -50;
    factions[0].law = 10;
    factions[1].oldOrder = 50;
    factions[1].law = -10;
    factions[2].oldOrder = -50;
    factions[2].law = 10;
    const PoliticsMap m = politics(w, factions, { 24, 6, 1 << 20 });
    for (int a = 0; a < 3; ++a) {
        CHECK(m.hostilityOf(a, a) == 0);
        for (int b = 0; b < 3; ++b) CHECK(m.hostilityOf(a, b) == m.hostilityOf(b, a));
    }
    CHECK(m.hostilityOf(0, 1) == 120);
    CHECK(m.hostilityOf(0, 2) == 0);
    CHECK(m.hostilityOf(1, 2) == 120);

    const PoliticsMap overlapped = politics(w, factions, { 24, 6, 1 });
    CHECK(overlapped.hostilityOf(0, 1) > m.hostilityOf(0, 1));
    CHECK(overlapped.hostilityOf(0, 2) > 0);
    CHECK(overlapped.hostilityOf(0, 1) <= 120 + 255);
}

TEST_CASE("ore under contested cells raises hostility", "[worldforge][politics]")
{
    World w = flatWorld(32, 32);
    w.sites = { { 4, 16 }, { 27, 16 } };
    const std::vector<FactionSpec> factions(2);
    const PoliticsMap barren = politics(w, factions, { 24, 6, 8 });
    for (std::size_t c = 0; c < w.resources.oreGrade.size(); ++c) w.resources.oreGrade[c] = 200;
    const PoliticsMap rich = politics(w, factions, { 24, 6, 8 });
    CHECK(rich.hostilityOf(0, 1) > barren.hostilityOf(0, 1));
}

TEST_CASE("homes follow scoring weights and honour fixed sites", "[worldforge][politics]")
{
    World w = flatWorld(48, 48);
    w.sites = { { 6, 6 }, { 40, 6 }, { 6, 40 }, { 40, 40 }, { 24, 24 } };
    w.resources.oreGrade[w.eroded.index(40, 6)] = 220;
    w.resources.arable[w.eroded.index(6, 40)] = 250;
    std::vector<FactionSpec> factions(3);
    factions[0].oreWeight = 10;
    factions[1].arableWeight = 10;
    factions[2].homeSite = 4;
    const PoliticsMap m = politics(w, factions);
    CHECK(m.homeSite[0] == 1);
    CHECK(m.homeSite[1] == 2);
    CHECK(m.homeSite[2] == 4);
    CHECK(m.homes[0].x == 40);
    CHECK(m.siteOwner.size() == 5);
    CHECK(m.siteOwner[1] == 0);
    CHECK(m.siteOwner[4] == 2);

    factions[2].homeSite = -1;
    factions[2].isolationWeight = 10;
    const PoliticsMap iso = politics(w, factions);
    CHECK(iso.homeSite[2] == 0);

    std::vector<FactionSpec> tied(2);
    const PoliticsMap tie = politics(w, tied);
    CHECK(tie.homeSite[0] == 0);
    CHECK(tie.homeSite[1] == 1);
}

TEST_CASE("politics reject malformed inputs and parameters", "[worldforge][politics][adversarial]")
{
    World w = flatWorld(32, 32);
    w.sites = { { 4, 4 }, { 20, 20 }, { 10, 25 } };
    const std::vector<FactionSpec> two(2);
    const auto gen = [&](const World& world, std::span<const FactionSpec> f, const PoliticsParams& p = {}) {
        return generatePolitics({ &world.eroded, &world.resources, world.sites, f }, p);
    };
    REQUIRE(gen(w, two).has_value());

    CHECK(!generatePolitics({ nullptr, &w.resources, w.sites, two }).has_value());
    CHECK(!generatePolitics({ &w.eroded, nullptr, w.sites, two }).has_value());

    CHECK(!gen(w, two, { -1, 6, 8 }).has_value());
    CHECK(!gen(w, two, { 4097, 6, 8 }).has_value());
    CHECK(!gen(w, two, { 24, 0, 8 }).has_value());
    CHECK(!gen(w, two, { 24, 2049, 8 }).has_value());
    CHECK(!gen(w, two, { 24, 6, 0 }).has_value());
    CHECK(!gen(w, two, { 24, 6, (1 << 20) + 1 }).has_value());
    CHECK(!gen(w, two, { INT_MIN, INT_MAX, INT_MIN }).has_value());

    World offMap = w;
    offMap.sites[0] = { 32, 4 };
    CHECK(!gen(offMap, two).has_value());
    offMap.sites[0] = { -1, 4 };
    CHECK(!gen(offMap, two).has_value());
    World onSea = w;
    onSea.eroded.heightM[onSea.eroded.index(4, 4)] = -1;
    CHECK(!gen(onSea, two).has_value());
    World dup = w;
    dup.sites[1] = dup.sites[0];
    CHECK(!gen(dup, two).has_value());
    World big = flatWorld(128, 128);
    for (int i = 0; i < kPoliticsMaxSites; ++i) big.sites.push_back({ static_cast<std::int16_t>(i % 128), static_cast<std::int16_t>(i / 128) });
    CHECK(gen(big, two).has_value());
    big.sites.push_back({ 0, 40 });
    CHECK(!gen(big, two).has_value());

    World crowded = flatWorld(64, 64);
    for (int i = 0; i < 20; ++i) crowded.sites.push_back({ static_cast<std::int16_t>(2 + 3 * (i % 10)), static_cast<std::int16_t>(2 + 3 * (i / 10)) });
    CHECK(gen(crowded, std::vector<FactionSpec>(16)).has_value());
    CHECK(!gen(crowded, std::vector<FactionSpec>(17)).has_value());
    const std::vector<FactionSpec> four(4);
    CHECK(!gen(w, four).has_value());

    const auto bad = [&](auto mutate) {
        std::vector<FactionSpec> f(2);
        mutate(f[1]);
        return gen(w, f);
    };
    CHECK(!bad([](FactionSpec& f) { f.strategy = static_cast<ClaimStrategy>(9); }).has_value());
    CHECK(!bad([](FactionSpec& f) { f.homeSite = 3; }).has_value());
    CHECK(!bad([](FactionSpec& f) { f.homeSite = -2; }).has_value());
    CHECK(!bad([](FactionSpec& f) { f.reach = -1; }).has_value());
    CHECK(!bad([](FactionSpec& f) { f.reach = 2049; }).has_value());
    CHECK(!bad([](FactionSpec& f) { f.decayPerCell = 65; }).has_value());
    CHECK(!bad([](FactionSpec& f) { f.oreWeight = 256; }).has_value());
    CHECK(!bad([](FactionSpec& f) { f.arableWeight = -1; }).has_value());
    CHECK(!bad([](FactionSpec& f) { f.isolationWeight = INT_MAX; }).has_value());
    CHECK(!bad([](FactionSpec& f) { f.oldOrder = 101; }).has_value());
    CHECK(!bad([](FactionSpec& f) { f.law = INT_MIN; }).has_value());

    std::vector<FactionSpec> sameHome(2);
    sameHome[0].homeSite = 1;
    sameHome[1].homeSite = 1;
    CHECK(!gen(w, sameHome).has_value());

    World otherSeed = w;
    otherSeed.resources.seed ^= 1;
    CHECK(!gen(otherSeed, two).has_value());
    World wrongSize = w;
    wrongSize.resources.arable.pop_back();
    CHECK(!gen(wrongSize, two).has_value());
    World wrongHeights = w;
    wrongHeights.eroded.heightM.pop_back();
    CHECK(!gen(wrongHeights, two).has_value());
    World hostileHeight = w;
    hostileHeight.eroded.heightM[3] = 30000;
    CHECK(!gen(hostileHeight, two).has_value());
    World tiny = flatWorld(8, 8);
    CHECK(!gen(tiny, {}).has_value());
    World empty;
    CHECK(!gen(empty, {}).has_value());
}

TEST_CASE("politics handle zero factions, zero sites and an all-ocean world", "[worldforge][politics][adversarial]")
{
    World w = flatWorld(kTectonicsMinSize, kTectonicsMinSize);
    const PoliticsMap none = politics(w, {});
    for (const std::uint8_t o : none.owner) CHECK(o == kPoliticsNoOwner);
    CHECK(none.hostility.empty());

    w.sites = { { 3, 3 } };
    const PoliticsMap lone = politics(w, ecology(1));
    for (const std::uint8_t o : lone.owner) CHECK(o == 0);
    CHECK(lone.territory[0] == static_cast<std::uint32_t>(kTectonicsMinSize * kTectonicsMinSize));
    CHECK(contestedCount(lone) == 0);

    World sea = flatWorld(kTectonicsMinSize, kTectonicsMinSize);
    for (auto& h : sea.eroded.heightM) h = static_cast<std::int16_t>(kTectonicsMinHeightM);
    const PoliticsMap empty = politics(sea, {});
    for (const std::uint8_t o : empty.owner) CHECK(o == kPoliticsNoOwner);
}

TEST_CASE("costs do not wrap on the largest grid with maximum decay", "[worldforge][politics][adversarial]")
{
    World w = flatWorld(kTectonicsMaxSize, kTectonicsMaxSize);
    w.sites = { { 0, 0 } };
    std::vector<FactionSpec> factions(1);
    factions[0].decayPerCell = kPoliticsMaxDecay;
    const PoliticsMap m = politics(w, factions);
    CHECK(m.territory[0] == static_cast<std::uint32_t>(kTectonicsMaxSize) * kTectonicsMaxSize);
}

TEST_CASE("politics scale to the largest grids and factions", "[worldforge][politics][adversarial]")
{
    World w = flatWorld(512, 512);
    for (int i = 0; i < kPoliticsMaxFactions; ++i) w.sites.push_back({ static_cast<std::int16_t>(16 + 30 * (i % 8)), static_cast<std::int16_t>(100 + 200 * (i / 8)) });
    const std::vector<FactionSpec> factions(kPoliticsMaxFactions);
    const PoliticsMap m = politics(w, factions);
    std::uint32_t total = 0;
    for (const std::uint32_t t : m.territory) total += t;
    CHECK(total == 512u * 512u);
}
