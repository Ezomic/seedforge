#include "ichor/core/dmath.h"
#include "ichor/stage0/recorder.h"
#include "ichor/stage0/swarm.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using namespace ichor;
using namespace ichor::stage0;

namespace {

const SwarmTuning kTuning{};
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

SwarmUnit unit(std::uint64_t id, float x, float z, bool holds = false)
{
    SwarmUnit u;
    u.id = id;
    u.pos = {x, z};
    u.holdsToken = holds;
    return u;
}

SwarmGroup ring(int count, float radius = 6.0f)
{
    SwarmGroup group;
    for (int i = 0; i < count; ++i) {
        SwarmMember m;
        m.id = static_cast<std::uint64_t>(i + 1);
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(std::max(count, 1));
        m.pos = {radius * dmath::cos(a), radius * dmath::sin(a)};
        group.members.push_back(m);
    }
    return group;
}

std::uint64_t hashGroup(const SwarmGroup& g)
{
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (const SwarmMember& m : g.members) {
        mix(m.id);
        std::uint32_t bx = 0;
        std::uint32_t bz = 0;
        std::memcpy(&bx, &m.pos.x, 4);
        std::memcpy(&bz, &m.pos.y, 4);
        mix(bx);
        mix(bz);
        mix(m.brain.state);
        mix(static_cast<std::uint64_t>(m.brain.stateTicks));
        mix(m.brain.token);
        mix(static_cast<std::uint64_t>(m.brain.cooldownTicks));
        mix(static_cast<std::uint64_t>(m.brain.strikes));
    }
    return h;
}

}

TEST_CASE("tokens: empty and single groups")
{
    CHECK(assignAttackTokens({}, {0, 0}, kTuning, 100).holders.empty());
    const auto one = assignAttackTokens({unit(7, 1.0f, 0.0f)}, {0, 0}, kTuning, 100);
    REQUIRE(one.holders == std::vector<std::uint64_t>{7});
    CHECK(one.granted);
    CHECK(one.newHolder == 7);
}

TEST_CASE("tokens: at most K holders in a group of 100 and the nearest win, ties by id")
{
    std::vector<SwarmUnit> units;
    for (int i = 0; i < 100; ++i) units.push_back(unit(static_cast<std::uint64_t>(100 - i), 1.0f, 0.0f)); // same spot
    const auto result = assignAttackTokens(units, {0, 0}, kTuning, 100);
    REQUIRE(result.granted);
    CHECK(result.newHolder == 1); // lowest id on a tie
    CHECK(result.holders.size() == 1);

    for (SwarmUnit& u : units) u.holdsToken = (u.id <= 5);
    const auto capped = assignAttackTokens(units, {0, 0}, kTuning, 100);
    CHECK(capped.holders == std::vector<std::uint64_t>{1, 2}); // K = 2, lowest ids kept
    CHECK_FALSE(capped.granted);
}

TEST_CASE("tokens: result does not depend on container order")
{
    std::vector<SwarmUnit> units;
    for (int i = 0; i < 20; ++i) units.push_back(unit(static_cast<std::uint64_t>(i + 1), 1.0f + static_cast<float>(i % 4), 0.5f));
    units[3].holdsToken = true;
    const auto a = assignAttackTokens(units, {0, 0}, kTuning, 100);
    std::reverse(units.begin(), units.end());
    const auto b = assignAttackTokens(units, {0, 0}, kTuning, 100);
    CHECK(a.holders == b.holders);
    CHECK(a.newHolder == b.newHolder);
    std::rotate(units.begin(), units.begin() + 7, units.end());
    CHECK(assignAttackTokens(units, {0, 0}, kTuning, 100).holders == a.holders);
}

TEST_CASE("tokens: the unit with the fewest strikes goes first even when a busier one is nearer")
{
    std::vector<SwarmUnit> units{unit(1, 1, 0), unit(2, 3, 0)};
    units[0].strikes = 4;
    CHECK(assignAttackTokens(units, {0, 0}, kTuning, 100).newHolder == 2);
}

TEST_CASE("tokens: the gap staggers grants")
{
    const std::vector<SwarmUnit> units{unit(1, 1, 0), unit(2, 1, 0), unit(3, 1, 0)};
    CHECK_FALSE(assignAttackTokens(units, {0, 0}, kTuning, kTuning.tokenGapTicks - 1).granted);
    CHECK(assignAttackTokens(units, {0, 0}, kTuning, kTuning.tokenGapTicks).granted);
}

TEST_CASE("tokens: a dead holder frees its slot, a unit on cooldown or out of range gets none")
{
    std::vector<SwarmUnit> units{unit(1, 1, 0, true), unit(2, 1, 0, true), unit(3, 2, 0), unit(4, 50, 0)};
    units[0].alive = false;
    const auto result = assignAttackTokens(units, {0, 0}, kTuning, 100);
    CHECK(result.holders == std::vector<std::uint64_t>{2, 3});
    CHECK(result.newHolder == 3);

    units[2].cooldownTicks = 5;
    CHECK(assignAttackTokens(units, {0, 0}, kTuning, 100).holders == std::vector<std::uint64_t>{2});
}

TEST_CASE("tokens: NaN positions never hold a token and a repeated id counts once")
{
    std::vector<SwarmUnit> units{unit(1, kNaN, 0, true), unit(2, 0, kNaN), unit(3, 1, 0), unit(3, 1, 0), unit(4, 1, 0)};
    const auto result = assignAttackTokens(units, {0, 0}, kTuning, 100);
    CHECK(result.holders == std::vector<std::uint64_t>{3});
    const auto nanTarget = assignAttackTokens(units, {kNaN, 0}, kTuning, 100);
    CHECK(nanTarget.holders.empty());
}

TEST_CASE("surround slots are evenly spread on the ring and survive a count of zero")
{
    const glm::vec2 centre{4.0f, -2.0f};
    CHECK(surroundSlot(0, 0, centre, 2.0f) == centre);
    for (int i = 0; i < 8; ++i) {
        const glm::vec2 s = surroundSlot(i, 8, centre, 2.0f);
        CHECK(std::abs(std::hypot(s.x - centre.x, s.y - centre.y) - 2.0f) < 1e-4f);
    }
    const glm::vec2 a = surroundSlot(0, 4, centre, 2.0f);
    const glm::vec2 c = surroundSlot(2, 4, centre, 2.0f);
    CHECK(std::abs((a.x + c.x) * 0.5f - centre.x) < 1e-4f);
}

TEST_CASE("a unit walks from Unaware to Approach to Surround and holds there without a token")
{
    SwarmBrain brain;
    SwarmSenses s;
    s.self = {10.0f, 0.0f};
    s.slot = {2.0f, 0.0f};
    swarmStep(brain, s, kTuning);
    CHECK(brain.swarmState() == SwarmState::Approach);
    s.self = {2.4f, 0.0f};
    swarmStep(brain, s, kTuning);
    CHECK(brain.swarmState() == SwarmState::Surround);
    for (int i = 0; i < 300; ++i) swarmStep(brain, s, kTuning);
    CHECK(brain.swarmState() == SwarmState::Surround);
}

TEST_CASE("a token holder strikes once after its wind-up, then releases the token into cooldown")
{
    SwarmBrain brain;
    brain.state = static_cast<std::uint8_t>(SwarmState::Surround);
    brain.token = true;
    SwarmSenses s;
    s.self = {1.0f, 0.0f};
    s.slot = {2.0f, 0.0f};
    int strikes = 0;
    int strikeTick = -1;
    for (int t = 1; t <= 200; ++t) {
        if (swarmStep(brain, s, kTuning).strike) {
            ++strikes;
            strikeTick = t;
        }
    }
    CHECK(strikes == 1);
    CHECK(strikeTick == 1 + kTuning.windupTicks); // one tick to enter Strike, then the wind-up
    CHECK_FALSE(brain.token);
    CHECK(brain.strikes == 1);
    CHECK(brain.swarmState() == SwarmState::Surround);
}

TEST_CASE("a dead target sends every unit back to Unaware and drops its token")
{
    SwarmBrain brain;
    brain.state = static_cast<std::uint8_t>(SwarmState::Strike);
    brain.token = true;
    SwarmSenses s;
    s.targetAlive = false;
    const SwarmIntent intent = swarmStep(brain, s, kTuning);
    CHECK(brain.swarmState() == SwarmState::Unaware);
    CHECK_FALSE(brain.token);
    CHECK_FALSE(intent.strike);
    CHECK(intent.move == glm::vec2(0.0f));
}

TEST_CASE("NaN senses make a unit stand still instead of poisoning its state")
{
    SwarmBrain brain;
    SwarmSenses s;
    s.self = {kNaN, 0.0f};
    const SwarmIntent intent = swarmStep(brain, s, kTuning);
    CHECK(intent.move == glm::vec2(0.0f));
    CHECK(brain.swarmState() == SwarmState::Unaware);
    s.self = {1.0f, 0.0f};
    s.target = {0.0f, kNaN};
    CHECK(swarmStep(brain, s, kTuning).move == glm::vec2(0.0f));
}

TEST_CASE("a group of 24 never has more than K committed, never strikes on the same tick, and everyone gets a turn")
{
    SwarmGroup group = ring(24);
    std::vector<int> struck(25, 0);
    int maxCommitted = 0;
    for (int t = 0; t < 3600; ++t) {
        const SwarmEvents events = tickSwarm(group, {{0.0f, 0.0f}, true}, kTuning);
        CHECK(events.strikes.size() <= 1);
        for (std::uint64_t id : events.strikes) struck[id] += 1;
        int committed = 0;
        for (const SwarmMember& m : group.members) {
            if (m.brain.token) ++committed;
        }
        maxCommitted = std::max(maxCommitted, committed);
    }
    CHECK(maxCommitted <= kTuning.maxAttackers);
    CHECK(maxCommitted >= 1);
    const auto [lo, hi] = std::minmax_element(struck.begin() + 1, struck.end());
    CHECK(*lo >= 1);
    CHECK(*hi - *lo <= 1);
}

TEST_CASE("two token holders in range on the same tick cannot both begin a wind-up")
{
    SwarmGroup group;
    for (std::uint64_t id = 1; id <= 2; ++id) {
        SwarmMember m;
        m.id = id;
        m.pos = {1.0f, static_cast<float>(id) * 0.1f};
        m.brain.state = static_cast<std::uint8_t>(SwarmState::Surround);
        m.brain.token = true;
        group.members.push_back(m);
    }
    group.ticksSinceGrant = 0;
    tickSwarm(group, {{0, 0}, true}, kTuning);
    const int striking = static_cast<int>(std::count_if(group.members.begin(), group.members.end(),
        [](const SwarmMember& m) { return m.brain.swarmState() == SwarmState::Strike; }));
    CHECK(striking == 1);
    CHECK(group.members[0].brain.swarmState() == SwarmState::Strike); // lower id first
}

TEST_CASE("duplicate ids and non-finite members do not take tokens or ring slots")
{
    SwarmGroup group = ring(4, 3.0f);
    group.members.push_back(group.members[0]);
    SwarmMember bad;
    bad.id = 99;
    bad.pos = {kNaN, 0.0f};
    group.members.push_back(bad);
    SwarmMember huge;
    huge.id = 100;
    huge.pos = {3e38f, 0.0f};
    group.members.push_back(huge);
    for (int t = 0; t < 600; ++t) {
        tickSwarm(group, {{0.0f, 0.0f}, true}, kTuning);
    }
    for (std::size_t i = 0; i < 4; ++i) CHECK(std::isfinite(group.members[i].pos.x));
    CHECK(group.members[4].brain.strikes == 0); // the duplicate never acts
    CHECK(group.members[5].brain.swarmState() == SwarmState::Unaware);
    CHECK(group.members[6].brain.swarmState() == SwarmState::Unaware);
}

TEST_CASE("a unit that is not yet in position is not handed a token")
{
    std::vector<SwarmUnit> units{unit(1, 1, 0), unit(2, 2, 0)};
    units[0].engaged = false;
    CHECK(assignAttackTokens(units, {0, 0}, kTuning, 100).newHolder == 2);
}

TEST_CASE("groups of 0 and 1 tick cleanly")
{
    SwarmGroup empty;
    CHECK(tickSwarm(empty, {{0, 0}, true}, kTuning).strikes.empty());
    SwarmGroup solo = ring(1, 5.0f);
    int strikes = 0;
    for (int t = 0; t < 600; ++t) strikes += static_cast<int>(tickSwarm(solo, {{0, 0}, true}, kTuning).strikes.size());
    CHECK(strikes >= 1);
}

TEST_CASE("all units on one spot, including the target's, stay finite")
{
    SwarmGroup group;
    for (int i = 0; i < 100; ++i) {
        SwarmMember m;
        m.id = static_cast<std::uint64_t>(i + 1);
        group.members.push_back(m);
    }
    for (int t = 0; t < 600; ++t) tickSwarm(group, {{0, 0}, true}, kTuning);
    for (const SwarmMember& m : group.members) {
        CHECK(std::isfinite(m.pos.x));
        CHECK(std::isfinite(m.pos.y));
    }
}

TEST_CASE("the token holder dying mid-attack hands the slot on")
{
    SwarmGroup group = ring(6, 3.0f);
    std::uint64_t holder = 0;
    for (int t = 0; t < 600 && holder == 0; ++t) {
        tickSwarm(group, {{0, 0}, true}, kTuning);
        for (const SwarmMember& m : group.members) {
            if (m.brain.token && m.brain.swarmState() == SwarmState::Strike) holder = m.id;
        }
    }
    REQUIRE(holder != 0);
    for (SwarmMember& m : group.members) {
        if (m.id == holder) m.alive = false;
    }
    int strikesAfter = 0;
    for (int t = 0; t < 600; ++t) {
        const SwarmEvents e = tickSwarm(group, {{0, 0}, true}, kTuning);
        for (std::uint64_t id : e.strikes) {
            CHECK(id != holder);
            ++strikesAfter;
        }
    }
    CHECK(strikesAfter > 0);
}

TEST_CASE("the target dying stops all strikes")
{
    SwarmGroup group = ring(6, 3.0f);
    for (int t = 0; t < 200; ++t) tickSwarm(group, {{0, 0}, true}, kTuning);
    for (int t = 0; t < 200; ++t) CHECK(tickSwarm(group, {{0, 0}, false}, kTuning).strikes.empty());
    for (const SwarmMember& m : group.members) CHECK_FALSE(m.brain.token);
}

TEST_CASE("the recorder logs swarm grants and strikes as foe attacks")
{
    FightRecorder recorder;
    recorder.noteSwarmGrant();
    recorder.noteSwarmStrike(1.0);
    recorder.noteSwarmStrike(2.0);
    CHECK(recorder.swarmGrants() == 1);
    CHECK(recorder.swarmStrikes() == 2);
    CHECK(recorder.running());
    CHECK(recorder.foe().attacks == 2);
}

TEST_CASE("the same inputs give the same hash, member order included")
{
    SwarmGroup a = ring(16);
    SwarmGroup b = ring(16);
    std::reverse(b.members.begin(), b.members.end());
    for (int t = 0; t < 900; ++t) {
        tickSwarm(a, {{0, 0}, true}, kTuning);
        tickSwarm(b, {{0, 0}, true}, kTuning);
    }
    std::sort(b.members.begin(), b.members.end(), [](const SwarmMember& l, const SwarmMember& r) { return l.id < r.id; });
    CHECK(hashGroup(a) == hashGroup(b));
    SwarmGroup c = ring(16);
    for (int t = 0; t < 900; ++t) tickSwarm(c, {{0, 0}, true}, kTuning);
    CHECK(hashGroup(a) == hashGroup(c));
}
