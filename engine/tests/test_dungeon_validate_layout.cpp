#include "ichor/graph/dungeon_validate_layout.h"
#include "ichor/graph/mission.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace ichor::graph;
using namespace ichor::graph::dungeon;
namespace m = ichor::graph::mission;

namespace {

bool has(const std::vector<Finding>& f, Code code, std::uint32_t room = 0)
{
    return std::any_of(f.begin(), f.end(), [&](const Finding& x) { return x.code == code && (room == 0 || x.room == room); });
}

// 1 entry, 2 junction, 3 key, 4 gate, 5 reward, 6 climax. The key sits on a
// side branch with a shortcut back, so nothing traps.
Layout good()
{
    Layout l;
    l.rooms = {{1, m::kEntry}, {2, m::kJunction}, {3, m::kKey}, {4, m::kGate}, {5, m::kReward}, {6, m::kClimax}};
    l.links = {{1, 2, m::kPath}, {2, 3, m::kPath}, {3, 2, m::kShortcut}, {2, 4, m::kPath}, {4, 5, m::kPath}, {5, 6, m::kPath}, {3, 4, m::kUnlocks}};
    return l;
}

void dropLinks(Layout& l, std::uint32_t from, std::uint32_t to)
{
    l.links.erase(std::remove_if(l.links.begin(), l.links.end(), [&](const Link& k) { return k.from == from && k.to == to; }), l.links.end());
}

}

TEST_CASE("a well formed dungeon validates clean", "[dungeon_validate]")
{
    CHECK(validateLayout(good()).empty());
}

TEST_CASE("an empty layout is rejected", "[dungeon_validate]")
{
    CHECK(has(validateLayout(Layout{}), Code::Empty));
}

TEST_CASE("an unreachable room is reported", "[dungeon_validate]")
{
    Layout l = good();
    l.rooms.push_back({7, m::kRest});
    const auto f = validateLayout(l);
    CHECK(has(f, Code::Unreachable, 7));
    CHECK(!has(f, Code::Unreachable, 6));
}

TEST_CASE("a gate placed before its key is reported", "[dungeon_validate]")
{
    Layout l = good();
    dropLinks(l, 2, 3);
    dropLinks(l, 3, 2);
    l.links.push_back({5, 3, m::kPath}); // the key is only past the gate it opens
    l.links.push_back({3, 6, m::kPath});
    const auto f = validateLayout(l);
    CHECK(has(f, Code::GateBeforeKey, 4));
    CHECK(has(f, Code::KeyNotObtainable, 3));
    CHECK(has(f, Code::Unreachable, 6));
}

TEST_CASE("a gate with no key is reported", "[dungeon_validate]")
{
    Layout l = good();
    l.links.pop_back();
    CHECK(has(validateLayout(l), Code::GateWithoutKey, 4));
}

TEST_CASE("a gate with two keys needs both", "[dungeon_validate]")
{
    Layout l = good();
    l.rooms.push_back({7, m::kKey});
    l.links.push_back({7, 4, m::kUnlocks});
    const auto f = validateLayout(l);
    CHECK(has(f, Code::Unreachable, 7));
    CHECK(has(f, Code::GateBeforeKey, 4));
    l.links.push_back({2, 7, m::kPath});
    l.links.push_back({7, 2, m::kShortcut});
    CHECK(validateLayout(l).empty());
}

TEST_CASE("a cycle that traps the player is a soft-lock", "[dungeon_validate]")
{
    Layout l = good();
    l.rooms.push_back({7, m::kChallenge});
    l.rooms.push_back({8, m::kChallenge});
    l.links.push_back({2, 7, m::kPath, true});
    l.links.push_back({7, 8, m::kPath, true});
    l.links.push_back({8, 7, m::kPath, true});
    const auto f = validateLayout(l);
    CHECK(has(f, Code::SoftLock, 7));
    CHECK(has(f, Code::SoftLock, 8));
    CHECK(!has(f, Code::SoftLock, 2));
}

TEST_CASE("a key room with no way back to its gate makes the objective unreachable", "[dungeon_validate]")
{
    Layout l;
    l.rooms = {{1, m::kEntry}, {2, m::kJunction}, {3, m::kKey}, {4, m::kGate}, {6, m::kClimax}};
    l.links = {{1, 2, m::kPath}, {2, 3, m::kPath, true}, {2, 4, m::kPath}, {4, 6, m::kPath}, {3, 4, m::kUnlocks}};
    const auto f = validateLayout(l);
    CHECK(has(f, Code::GateBeforeKey, 4));
    CHECK(has(f, Code::Unreachable, 6));
    l.links.push_back({3, 2, m::kShortcut});
    CHECK(validateLayout(l).empty());
}

TEST_CASE("an unlock link must run from a key to a gate", "[dungeon_validate]")
{
    Layout l = good();
    l.links.push_back({2, 4, m::kUnlocks});
    CHECK(has(validateLayout(l), Code::BadUnlock));
    Layout k = good();
    k.links.push_back({3, 5, m::kUnlocks});
    CHECK(has(validateLayout(k), Code::BadUnlock));
}

TEST_CASE("more keys than the search supports is refused", "[dungeon_validate]")
{
    Layout l = good();
    for (std::uint32_t i = 0; i <= kMaxKeys; ++i) {
        l.rooms.push_back({10 + i, m::kKey});
        l.links.push_back({2, 10 + i, m::kPath});
        l.links.push_back({10 + i, 2, m::kShortcut});
    }
    const auto f = validateLayout(l);
    REQUIRE(f.size() == 1);
    CHECK(f[0].code == Code::TooLarge);
}

TEST_CASE("the soft-lock check is what flags a trap, not reachability", "[dungeon_validate]")
{
    Layout l = good();
    l.rooms.push_back({7, m::kRest});
    l.links.push_back({2, 7, m::kPath, true});
    const auto f = validateLayout(l);
    CHECK(!has(f, Code::Unreachable));
    CHECK(has(f, Code::SoftLock, 7));
    l.links.push_back({7, 2, m::kShortcut});
    CHECK(validateLayout(l).empty());
}

TEST_CASE("a dungeon with no climax has no objective", "[dungeon_validate]")
{
    Layout l = good();
    l.rooms.pop_back();
    dropLinks(l, 5, 6);
    CHECK(has(validateLayout(l), Code::NoObjective));
}

TEST_CASE("entry count must be exactly one", "[dungeon_validate]")
{
    Layout none = good();
    none.rooms[0].type = m::kJunction;
    CHECK(has(validateLayout(none), Code::EntryCount));
    Layout two = good();
    two.rooms.push_back({7, m::kEntry});
    CHECK(has(validateLayout(two), Code::EntryCount));
}

TEST_CASE("duplicate ids and dangling links are rejected", "[dungeon_validate]")
{
    Layout dup = good();
    dup.rooms.push_back({3, m::kRest});
    CHECK(has(validateLayout(dup), Code::DuplicateId, 3));
    Layout dangling = good();
    dangling.links.push_back({2, 99, m::kPath});
    CHECK(has(validateLayout(dangling), Code::DanglingLink));
    Layout zero = good();
    zero.rooms.push_back({0, m::kRest});
    CHECK(has(validateLayout(zero), Code::DuplicateId));
}

TEST_CASE("unknown room and link types are rejected", "[dungeon_validate]")
{
    Layout t = good();
    t.rooms[1].type = "seg";
    CHECK(has(validateLayout(t), Code::UnknownType, 2));
    Layout e = good();
    e.links[0].type = "teleport";
    CHECK(has(validateLayout(e), Code::UnknownType));
}

TEST_CASE("an oversized layout is refused up front", "[dungeon_validate]")
{
    Layout big;
    big.rooms.push_back({1, m::kEntry});
    for (std::uint32_t i = 2; i <= kMaxRooms + 1; ++i) {
        big.rooms.push_back({i, m::kChallenge});
        big.links.push_back({i - 1, i, m::kPath});
    }
    const auto f = validateLayout(big);
    REQUIRE(f.size() == 1);
    CHECK(f[0].code == Code::TooLarge);

    Layout links = good();
    for (std::size_t i = 0; i <= kMaxLinks; ++i) {
        links.links.push_back({1, 2, m::kPath});
    }
    const auto g = validateLayout(links);
    REQUIRE(g.size() == 1);
    CHECK(g[0].code == Code::TooLarge);
}

TEST_CASE("a layout at the size limit validates", "[dungeon_validate]")
{
    Layout l;
    l.rooms.push_back({1, m::kEntry});
    for (std::uint32_t i = 2; i < kMaxRooms; ++i) {
        l.rooms.push_back({i, m::kChallenge});
        l.links.push_back({i - 1, i, m::kPath});
        l.links.push_back({i, i - 1, m::kShortcut});
    }
    l.rooms.push_back({kMaxRooms, m::kClimax});
    l.links.push_back({kMaxRooms - 1, kMaxRooms, m::kPath});
    CHECK(validateLayout(l).empty());
}

TEST_CASE("a layout with the maximum keys and rooms validates", "[dungeon_validate]")
{
    Layout l;
    l.rooms.push_back({1, m::kEntry});
    std::uint32_t next = 2;
    std::uint32_t last = 1;
    for (std::uint32_t k = 0; k < kMaxKeys; ++k) {
        const std::uint32_t key = next++;
        l.rooms.push_back({key, m::kKey});
        l.links.push_back({last, key, m::kPath});
        const std::uint32_t gate = next++;
        l.rooms.push_back({gate, m::kGate});
        l.links.push_back({last, gate, m::kPath});
        l.links.push_back({key, gate, m::kUnlocks});
        last = gate;
    }
    while (next < kMaxRooms) {
        l.rooms.push_back({next, m::kChallenge});
        l.links.push_back({last, next, m::kPath});
        last = next++;
    }
    l.rooms.push_back({next, m::kClimax});
    l.links.push_back({last, next, m::kPath});
    REQUIRE(l.rooms.size() == kMaxRooms);
    CHECK(validateLayout(l).empty());
}

TEST_CASE("generated missions validate clean", "[dungeon_validate]")
{
    const m::Ruleset rules = m::loadRuleset(std::string(ICHOR_REPO_DATA) + "/dungeon_missions/mission_rules.json");
    REQUIRE(rules.error.empty());
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        m::Params p;
        p.seed = seed;
        const m::Result r = m::generate(rules, p);
        REQUIRE(r.error.empty());
        const auto f = validateLayout(fromGraph(r.graph));
        INFO("seed " << seed);
        for (const Finding& x : f) {
            INFO(x.message);
        }
        CHECK(f.empty());
    }
}
