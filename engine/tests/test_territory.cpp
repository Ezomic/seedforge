#include "ichor/factions/territory.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

TerritoryGrid openGrid(std::uint32_t w, std::uint32_t h, std::uint16_t cost = 1)
{
    TerritoryGrid g;
    g.width = w;
    g.height = h;
    g.cost.assign(std::size_t{w} * h, cost);
    g.resource.assign(std::size_t{w} * h, 0);
    g.node.assign(std::size_t{w} * h, 0);
    return g;
}

ClaimSpec spec(std::string id, ClaimStrategy s, std::vector<std::uint32_t> homes, std::uint32_t reach)
{
    ClaimSpec c;
    c.id = std::move(id);
    c.strategy = s;
    c.homes = std::move(homes);
    c.reach = reach;
    return c;
}

std::size_t claimed(const std::vector<std::uint16_t>& s)
{
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](std::uint16_t v) { return v > 0; }));
}

bool run(const TerritoryGrid& g, const std::vector<ClaimSpec>& specs, TerritoryResult& out, std::vector<std::string>& problems, std::uint32_t time = 0)
{
    return claimTerritory(g, specs, time, out, problems);
}

}

TEST_CASE("strategy names round trip", "[territory]")
{
    for (ClaimStrategy s : {ClaimStrategy::ResourceFollowing, ClaimStrategy::ContiguousDecay, ClaimStrategy::NodesOnly, ClaimStrategy::DefensiveIsolated, ClaimStrategy::RoutesNotTerritory, ClaimStrategy::ExpandingOverTime}) {
        REQUIRE(claimStrategyFromName(claimStrategyName(s)) == s);
    }
    REQUIRE_FALSE(claimStrategyFromName("Contiguous").has_value());
    REQUIRE_FALSE(claimStrategyFromName("").has_value());
}

TEST_CASE("contiguous decay is strongest at home and falls with distance", "[territory]")
{
    TerritoryGrid g = openGrid(11, 1);
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {spec("a", ClaimStrategy::ContiguousDecay, {0}, 5)}, r, p));
    const auto& s = r.strength[0];
    REQUIRE(s[0] == kMaxClaimStrength);
    for (std::size_t i = 1; i <= 4; ++i) {
        REQUIRE(s[i] < s[i - 1]);
        REQUIRE(s[i] > 0);
    }
    REQUIRE(claimed(s) == 5);
    REQUIRE(s[5] == 0);
}

TEST_CASE("costly terrain shortens reach and walls stop it", "[territory]")
{
    TerritoryGrid g = openGrid(10, 1);
    g.cost[2] = 3;
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {spec("a", ClaimStrategy::ContiguousDecay, {0}, 6)}, r, p));
    REQUIRE(claimed(r.strength[0]) == 4);

    g.cost[2] = 0;
    REQUIRE(run(g, {spec("a", ClaimStrategy::ContiguousDecay, {0}, 6)}, r, p));
    REQUIRE(claimed(r.strength[0]) == 2);
    REQUIRE(r.strength[0][2] == 0);
    REQUIRE(r.strength[0][3] == 0);
}

TEST_CASE("fill goes around obstacles by the cheaper route", "[territory]")
{
    TerritoryGrid g = openGrid(5, 3);
    g.cost[1 * 5 + 1] = 0;
    g.cost[1 * 5 + 2] = 0;
    g.cost[1 * 5 + 3] = 0;
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {spec("a", ClaimStrategy::ContiguousDecay, {1 * 5 + 0}, 20)}, r, p));
    REQUIRE(r.strength[0][1 * 5 + 4] > 0);
    REQUIRE(r.strength[0][1 * 5 + 4] < r.strength[0][0]);
}

TEST_CASE("resource following claims rich cells across poor ground and ignores other factions", "[territory]")
{
    TerritoryGrid g = openGrid(12, 1);
    g.resource[6] = 200;
    g.resource[7] = 255;
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {spec("metal", ClaimStrategy::ResourceFollowing, {0}, 8), spec("other", ClaimStrategy::ContiguousDecay, {11}, 8)}, r, p));
    const auto& m = r.strength[0];
    REQUIRE(m[6] > 0);
    REQUIRE(m[7] > m[6]);
    REQUIRE(m[3] == 0);
    REQUIRE(m[0] == 0);
    REQUIRE(r.strength[1][6] > 0);
    REQUIRE(r.strength[1][7] > 0);
}

TEST_CASE("resource following honours minResource and reach", "[territory]")
{
    TerritoryGrid g = openGrid(12, 1);
    g.resource[3] = 10;
    g.resource[9] = 255;
    ClaimSpec c = spec("metal", ClaimStrategy::ResourceFollowing, {0}, 5);
    c.minResource = 50;
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {c}, r, p));
    REQUIRE(claimed(r.strength[0]) == 0);
}

TEST_CASE("nodes only owns the harbours and not the land between", "[territory]")
{
    TerritoryGrid g = openGrid(10, 1);
    g.node[0] = 1;
    g.node[4] = 1;
    g.node[9] = 1;
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {spec("ports", ClaimStrategy::NodesOnly, {0}, 5)}, r, p));
    const auto& s = r.strength[0];
    REQUIRE(s[0] > 0);
    REQUIRE(s[4] > 0);
    REQUIRE(s[9] == 0);
    REQUIRE(claimed(s) == 2);
}

TEST_CASE("routes not territory claims nothing", "[territory]")
{
    TerritoryGrid g = openGrid(6, 6);
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {spec("xenoi", ClaimStrategy::RoutesNotTerritory, {7}, 50)}, r, p));
    REQUIRE(r.strength.size() == 1);
    REQUIRE(r.strength[0].size() == 36);
    REQUIRE(claimed(r.strength[0]) == 0);
}

TEST_CASE("defensive isolated stays clear of other homes", "[territory]")
{
    TerritoryGrid g = openGrid(12, 1);
    ClaimSpec d = spec("asebeis", ClaimStrategy::DefensiveIsolated, {5}, 10);
    d.avoidRadius = 3;
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {d, spec("archonate", ClaimStrategy::ContiguousDecay, {11}, 3)}, r, p));
    const auto& s = r.strength[0];
    REQUIRE(s[5] > 0);
    REQUIRE(s[0] > 0);
    REQUIRE(s[7] > 0);
    REQUIRE(s[8] == 0);
    REQUIRE(s[11] == 0);
    REQUIRE(r.strength[1][11] > 0);
}

TEST_CASE("expanding over time grows with campaign time", "[territory]")
{
    TerritoryGrid g = openGrid(30, 1);
    ClaimSpec h = spec("herald", ClaimStrategy::ExpandingOverTime, {0}, 3);
    h.growthPerTime = 2;
    TerritoryResult r0, r1, r2;
    std::vector<std::string> p;
    REQUIRE(run(g, {h}, r0, p, 0));
    REQUIRE(run(g, {h}, r1, p, 5));
    REQUIRE(run(g, {h}, r2, p, 5000000));
    REQUIRE(claimed(r0.strength[0]) == 3);
    REQUIRE(claimed(r1.strength[0]) == 13);
    REQUIRE(claimed(r2.strength[0]) == 30);
}

TEST_CASE("extreme reach and time do not overflow", "[territory]")
{
    TerritoryGrid g = openGrid(8, 8, std::numeric_limits<std::uint16_t>::max());
    ClaimSpec h = spec("herald", ClaimStrategy::ExpandingOverTime, {0}, std::numeric_limits<std::uint32_t>::max());
    h.growthPerTime = std::numeric_limits<std::uint32_t>::max();
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {h}, r, p, std::numeric_limits<std::uint32_t>::max()));
    REQUIRE(claimed(r.strength[0]) == 64);
    REQUIRE(r.strength[0][0] == kMaxClaimStrength);
    for (std::uint16_t v : r.strength[0]) {
        REQUIRE(v <= kMaxClaimStrength);
    }
}

TEST_CASE("zero reach claims only the home", "[territory]")
{
    TerritoryGrid g = openGrid(5, 5);
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {spec("a", ClaimStrategy::ContiguousDecay, {12}, 0)}, r, p));
    REQUIRE(claimed(r.strength[0]) == 1);
    REQUIRE(r.strength[0][12] == kMaxClaimStrength);
}

TEST_CASE("multiple homes claim the union", "[territory]")
{
    TerritoryGrid g = openGrid(20, 1);
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {spec("a", ClaimStrategy::ContiguousDecay, {0, 19}, 3)}, r, p));
    REQUIRE(claimed(r.strength[0]) == 6);
}

TEST_CASE("results are deterministic and the digest tracks them", "[territory]")
{
    TerritoryGrid g = openGrid(16, 16);
    for (std::size_t i = 0; i < g.cost.size(); ++i) {
        g.cost[i] = static_cast<std::uint16_t>(1 + (i * 7) % 5);
        g.resource[i] = static_cast<std::uint8_t>((i * 31) % 256);
    }
    const std::vector<ClaimSpec> specs = {spec("a", ClaimStrategy::ContiguousDecay, {17, 200}, 12), spec("b", ClaimStrategy::ResourceFollowing, {100}, 15)};
    TerritoryResult r1, r2;
    std::vector<std::string> p;
    REQUIRE(run(g, specs, r1, p));
    REQUIRE(run(g, specs, r2, p));
    REQUIRE(r1.strength == r2.strength);
    REQUIRE(territoryDigest(r1) == territoryDigest(r2));
    r2.strength[0][0] ^= 1;
    REQUIRE(territoryDigest(r1) != territoryDigest(r2));
}

TEST_CASE("bad input is reported and never crashes", "[territory]")
{
    TerritoryGrid g = openGrid(4, 4);
    TerritoryResult r;

    auto bad = [&](const TerritoryGrid& grid, const std::vector<ClaimSpec>& specs, const std::string& needle) {
        std::vector<std::string> p;
        REQUIRE_FALSE(run(grid, specs, r, p));
        REQUIRE_FALSE(p.empty());
        bool found = false;
        for (const std::string& line : p) {
            REQUIRE(line.rfind("territory: ", 0) == 0);
            found = found || line.find(needle) != std::string::npos;
        }
        REQUIRE(found);
    };

    bad(g, {spec("a", ClaimStrategy::ContiguousDecay, {16}, 3)}, "home");
    bad(g, {spec("a", ClaimStrategy::ContiguousDecay, {std::numeric_limits<std::uint32_t>::max()}, 3)}, "home");
    bad(g, {spec("a", ClaimStrategy::ContiguousDecay, {1}, 3), spec("a", ClaimStrategy::NodesOnly, {2}, 3)}, "duplicate");
    bad(g, {spec("", ClaimStrategy::ContiguousDecay, {1}, 3)}, "id");
    bad(g, {spec("a", ClaimStrategy::ContiguousDecay, {}, 3)}, "home");
    bad(g, {spec("a", ClaimStrategy::ContiguousDecay, {1, 1}, 3)}, "duplicate home");

    TerritoryGrid zero;
    bad(zero, {spec("a", ClaimStrategy::ContiguousDecay, {0}, 3)}, "grid");

    TerritoryGrid shortLayer = g;
    shortLayer.cost.pop_back();
    bad(shortLayer, {spec("a", ClaimStrategy::ContiguousDecay, {0}, 3)}, "cost");

    TerritoryGrid huge;
    huge.width = std::numeric_limits<std::uint32_t>::max();
    huge.height = std::numeric_limits<std::uint32_t>::max();
    bad(huge, {spec("a", ClaimStrategy::ContiguousDecay, {0}, 3)}, "grid");

    std::vector<ClaimSpec> many;
    for (std::size_t i = 0; i <= kMaxTerritoryFactions; ++i) {
        many.push_back(spec("f" + std::to_string(i), ClaimStrategy::RoutesNotTerritory, {0}, 1));
    }
    bad(g, many, "factions");

    std::vector<std::uint32_t> crowd;
    for (std::uint32_t i = 0; i < 300; ++i) {
        crowd.push_back(i % 16);
    }
    bad(g, {spec("a", ClaimStrategy::ContiguousDecay, crowd, 3)}, "home");
}

TEST_CASE("nodes only always owns its own home and rejects unknown strategies", "[territory]")
{
    TerritoryGrid g = openGrid(6, 1);
    g.node[4] = 1;
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {spec("ports", ClaimStrategy::NodesOnly, {0}, 6)}, r, p));
    REQUIRE(r.strength[0][0] == kMaxClaimStrength);
    REQUIRE(r.strength[0][4] > 0);
    REQUIRE(claimed(r.strength[0]) == 2);

    p.clear();
    REQUIRE_FALSE(run(g, {spec("x", static_cast<ClaimStrategy>(99), {0}, 6)}, r, p));
    REQUIRE(p.front().find("strategy") != std::string::npos);
}

TEST_CASE("no factions is a valid empty result", "[territory]")
{
    TerritoryGrid g = openGrid(3, 3);
    TerritoryResult r;
    std::vector<std::string> p;
    REQUIRE(run(g, {}, r, p));
    REQUIRE(r.strength.empty());
    REQUIRE(p.empty());
}

TEST_CASE("failed run leaves the result empty", "[territory]")
{
    TerritoryGrid g = openGrid(3, 3);
    TerritoryResult r;
    r.ids = {"stale"};
    std::vector<std::string> p;
    REQUIRE_FALSE(run(g, {spec("a", ClaimStrategy::ContiguousDecay, {99}, 3)}, r, p));
    REQUIRE(r.ids.empty());
    REQUIRE(r.strength.empty());
}
