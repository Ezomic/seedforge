#include "ichor/factions/hostility.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

FactionStance st(const char* id, OrderStance o, LawStance l)
{
    return FactionStance{id, o, l};
}

FactionClaim cl(const char* id, std::vector<std::uint32_t> cells, std::vector<std::uint32_t> res = {})
{
    return FactionClaim{id, std::move(cells), std::move(res)};
}

}

TEST_CASE("ideological distance is steps on both axes", "[hostility]")
{
    auto a = st("a", OrderStance::Restore, LawStance::Control);
    CHECK(ideologicalDistance(a, a) == 0);
    CHECK(ideologicalDistance(a, st("b", OrderStance::Preserve, LawStance::Control)) == 250);
    CHECK(ideologicalDistance(a, st("b", OrderStance::Replace, LawStance::Control)) == 500);
    auto b = st("b", OrderStance::Replace, LawStance::Renounce);
    CHECK(ideologicalDistance(a, b) == kMaxIdeology);
    CHECK(ideologicalDistance(b, a) == kMaxIdeology);
}

TEST_CASE("resource overlap is shared claim over the smaller claim", "[hostility]")
{
    CHECK(resourceOverlap(cl("a", {}), cl("b", {})) == 0);
    CHECK(resourceOverlap(cl("a", {}), cl("b", {1})) == 0);
    CHECK(resourceOverlap(cl("a", {1, 2}), cl("b", {3, 4})) == 0);
    CHECK(resourceOverlap(cl("a", {1, 2}), cl("b", {1, 2})) == kMaxOverlap);
    CHECK(resourceOverlap(cl("a", {1, 2, 3, 4}), cl("b", {1, 9})) == 500);
    CHECK(resourceOverlap(cl("a", {1, 1, 1, 2}), cl("b", {2})) == kMaxOverlap);
    CHECK(resourceOverlap(cl("a", {}, {7}), cl("b", {}, {7})) == kMaxOverlap);
    CHECK(resourceOverlap(cl("a", {1}, {7}), cl("b", {2}, {7})) == 500);
    CHECK(resourceOverlap(cl("a", {1}), cl("b", {}, {1})) == 0);
    auto x = cl("a", {1, 2});
    auto y = cl("b", {2, 1, 2, 1, 5});
    CHECK(resourceOverlap(x, y) == resourceOverlap(y, x));
    const std::uint32_t big = 0xFFFFFFFFu;
    CHECK(resourceOverlap(cl("a", {0, big}), cl("b", {big})) == kMaxOverlap);
}

TEST_CASE("matrix is symmetric, sorted, and classifies pairs", "[hostility]")
{
    std::vector<FactionStance> s = {st("c", OrderStance::Replace, LawStance::Renounce), st("a", OrderStance::Restore, LawStance::Control),
        st("b", OrderStance::Restore, LawStance::Control)};
    std::vector<FactionClaim> c = {cl("a", {1, 2}), cl("b", {1, 2}), cl("c", {9})};
    std::vector<HostilityEntry> m;
    std::vector<std::string> p;
    REQUIRE(computeHostilityMatrix(s, c, m, p));
    REQUIRE(m.size() == 3);
    CHECK(m[0].a == "a");
    CHECK(m[0].b == "b");
    CHECK(m[1].a == "a");
    CHECK(m[1].b == "c");
    CHECK(m[2].a == "b");
    CHECK(m[2].b == "c");

    const auto* ab = findHostility(m, "b", "a");
    REQUIRE(ab != nullptr);
    CHECK(ab->ideology == 0);
    CHECK(ab->overlap == kMaxOverlap);
    CHECK(ab->hostility == 1000);
    CHECK(ab->relation == Relation::Hostile);

    const auto* ac = findHostility(m, "a", "c");
    REQUIRE(ac != nullptr);
    CHECK(ac->ideology == kMaxIdeology);
    CHECK(ac->hostility == 1000);
    CHECK(findHostility(m, "a", "zzz") == nullptr);
    CHECK(findHostility(m, "a", "a") == nullptr);
}

TEST_CASE("odd bedfellows are opposed on an axis but not hostile", "[hostility]")
{
    std::vector<FactionStance> s = {st("a", OrderStance::Restore, LawStance::Free), st("b", OrderStance::Replace, LawStance::Free),
        st("c", OrderStance::Preserve, LawStance::Free)};
    std::vector<HostilityEntry> m;
    std::vector<std::string> p;
    REQUIRE(computeHostilityMatrix(s, {}, m, p));
    CHECK(findHostility(m, "a", "b")->oddBedfellows);
    CHECK(findHostility(m, "a", "b")->relation == Relation::Neutral);
    CHECK_FALSE(findHostility(m, "a", "c")->oddBedfellows);
    CHECK(findHostility(m, "a", "c")->relation == Relation::Allied);
    CHECK(findHostility(m, "a", "c")->hostility == 250);

    std::vector<FactionClaim> shared = {cl("a", {1}), cl("b", {1})};
    REQUIRE(computeHostilityMatrix(s, shared, m, p));
    CHECK(findHostility(m, "a", "b")->relation == Relation::Hostile);
    CHECK_FALSE(findHostility(m, "a", "b")->oddBedfellows);
}

TEST_CASE("kindred factions with no overlap are allied", "[hostility]")
{
    std::vector<FactionStance> s = {st("a", OrderStance::Restore, LawStance::Free), st("b", OrderStance::Restore, LawStance::Free)};
    std::vector<HostilityEntry> m;
    std::vector<std::string> p;
    REQUIRE(computeHostilityMatrix(s, {cl("a", {1}), cl("b", {2})}, m, p));
    CHECK(m[0].relation == Relation::Allied);
}

TEST_CASE("territory changes the matrix", "[hostility]")
{
    std::vector<FactionStance> s = {st("a", OrderStance::Restore, LawStance::Free), st("b", OrderStance::Preserve, LawStance::Free)};
    std::vector<HostilityEntry> none, shared;
    std::vector<std::string> p;
    REQUIRE(computeHostilityMatrix(s, {cl("a", {1}), cl("b", {2})}, none, p));
    REQUIRE(computeHostilityMatrix(s, {cl("a", {1}), cl("b", {1})}, shared, p));
    CHECK(shared[0].hostility == none[0].hostility + kMaxOverlap);
    CHECK(hostilityDigest(none) != hostilityDigest(shared));
}

TEST_CASE("input order does not change the result", "[hostility]")
{
    std::vector<FactionStance> s = {st("a", OrderStance::Restore, LawStance::Free), st("b", OrderStance::Replace, LawStance::Control),
        st("c", OrderStance::Preserve, LawStance::Renounce)};
    std::vector<FactionClaim> c = {cl("a", {1, 2}), cl("b", {2, 3}), cl("c", {3})};
    std::vector<HostilityEntry> m1, m2;
    std::vector<std::string> p;
    REQUIRE(computeHostilityMatrix(s, c, m1, p));
    std::reverse(s.begin(), s.end());
    std::reverse(c.begin(), c.end());
    REQUIRE(computeHostilityMatrix(s, c, m2, p));
    CHECK(hostilityDigest(m1) == hostilityDigest(m2));
}

TEST_CASE("bad input is reported, never crashes", "[hostility]")
{
    std::vector<HostilityEntry> m;
    std::vector<std::string> p;

    CHECK(computeHostilityMatrix({}, {}, m, p));
    CHECK(m.empty());
    CHECK(computeHostilityMatrix({st("a", OrderStance::Restore, LawStance::Free)}, {}, m, p));
    CHECK(m.empty());
    CHECK(p.empty());

    const auto seed = [&] {
        p.clear();
        REQUIRE(computeHostilityMatrix({st("x", OrderStance::Restore, LawStance::Free), st("y", OrderStance::Restore, LawStance::Free)}, {}, m, p));
        REQUIRE(m.size() == 1);
    };
    const auto mentions = [&](const char* word) {
        return std::any_of(p.begin(), p.end(), [&](const std::string& line) { return line.find(word) != std::string::npos; });
    };

    seed();
    CHECK_FALSE(computeHostilityMatrix({st("a", OrderStance::Restore, LawStance::Free), st("a", OrderStance::Replace, LawStance::Free)}, {}, m, p));
    CHECK(m.empty());
    CHECK(mentions("duplicate"));

    seed();
    CHECK_FALSE(computeHostilityMatrix({st("a", OrderStance::Restore, LawStance::Free)}, {cl("ghost", {1})}, m, p));
    CHECK(m.empty());
    CHECK(mentions("unknown"));

    seed();
    CHECK_FALSE(computeHostilityMatrix({st("a", OrderStance::Restore, LawStance::Free)}, {cl("a", {1}), cl("a", {2})}, m, p));
    CHECK(m.empty());
    CHECK(mentions("duplicate"));

    seed();
    FactionStance badOrder{"a", static_cast<OrderStance>(9), LawStance::Free};
    CHECK_FALSE(computeHostilityMatrix({badOrder}, {}, m, p));
    CHECK(m.empty());
    CHECK(mentions("out of range"));

    seed();
    FactionStance badLaw{"a", OrderStance::Restore, static_cast<LawStance>(9)};
    CHECK_FALSE(computeHostilityMatrix({badLaw}, {}, m, p));
    CHECK(m.empty());
    CHECK(mentions("out of range"));
}

TEST_CASE("many factions stay bounded and consistent", "[hostility]")
{
    std::vector<FactionStance> s;
    for (int i = 0; i < 40; ++i)
        s.push_back(st(("f" + std::to_string(i)).c_str(), static_cast<OrderStance>(i % 3), static_cast<LawStance>((i / 3) % 3)));
    std::vector<HostilityEntry> m;
    std::vector<std::string> p;
    REQUIRE(computeHostilityMatrix(s, {}, m, p));
    CHECK(m.size() == 40u * 39u / 2u);
    for (const auto& e : m) {
        CHECK(e.hostility >= 0);
        CHECK(e.hostility <= kMaxHostility);
        CHECK(e.a < e.b);
    }
}
