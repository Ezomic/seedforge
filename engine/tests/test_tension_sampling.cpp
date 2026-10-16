#include "ichor/ledger/tension_sampling.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

HostilityEntry hostile(std::string a, std::string b, std::int32_t hostility)
{
    HostilityEntry e;
    e.a = std::move(a);
    e.b = std::move(b);
    e.hostility = hostility;
    e.relation = hostility >= kHostileAtLeast ? Relation::Hostile : Relation::Neutral;
    return e;
}

TerritoryResult twoFactions(std::size_t cells)
{
    TerritoryResult t;
    t.ids = {"ash", "brine"};
    t.strength.assign(2, std::vector<std::uint16_t>(cells, 0));
    return t;
}

TensionConfig config(std::uint64_t seed, std::size_t cap)
{
    TensionConfig c;
    c.seed = seed;
    c.cap = cap;
    return c;
}

std::size_t countKept(const std::vector<Tension>& list, const std::string& wanter)
{
    std::size_t n = 0;
    for (const Tension& t : list) {
        n += t.wanter == wanter ? 1 : 0;
    }
    return n;
}

}

TEST_CASE("an empty world has no tensions", "[tension]")
{
    CHECK(collectTensions({}, config(1, 8)).empty());
    CHECK(sampleTensions({}, config(1, 8)).empty());

    ConsequenceLedger ledger;
    std::vector<HostilityEntry> matrix;
    TerritoryResult territory;
    TensionInputs in;
    in.ledger = &ledger;
    in.hostility = &matrix;
    in.territory = &territory;
    CHECK(sampleTensions(in, config(1, 8)).empty());
}

TEST_CASE("each source yields who wants what from whom", "[tension]")
{
    ConsequenceLedger ledger;
    REQUIRE(ledger.append(Fact{"f1", "kessen", "starved", "1", 100}) == AppendResult::Ok);
    std::vector<HostilityEntry> matrix = {hostile("b", "a", 1500)};
    TerritoryResult territory = twoFactions(4);
    territory.strength[0] = {0, 900, 500, 0};
    territory.strength[1] = {0, 300, 800, 700};

    TensionInputs in;
    in.ledger = &ledger;
    in.ledgerQuery.kinds = {{"starved", 10}};
    in.ledgerQuery.now = 100;
    in.hostility = &matrix;
    in.territory = &territory;

    const auto all = collectTensions(in, config(1, 8));
    REQUIRE(all.size() == 3);
    for (const Tension& t : all) {
        if (t.kind == TensionKind::Consequence) {
            CHECK(t.wanter == "kessen");
            CHECK(t.holder == "player");
            CHECK(t.stake == "fact:f1");
            CHECK(t.score == 10000);
        } else if (t.kind == TensionKind::Hostility) {
            CHECK(t.wanter == "a");
            CHECK(t.holder == "b");
            CHECK(t.score == 1500000);
        } else {
            CHECK(t.wanter == "ash");
            CHECK(t.holder == "brine");
            CHECK(t.stake == "cell:1");
            CHECK(t.score == 300 + 500);
        }
    }
    CHECK(all[0].kind == TensionKind::Hostility);
}

TEST_CASE("below-threshold hostility and uncontested cells are not tensions", "[tension]")
{
    std::vector<HostilityEntry> matrix = {hostile("a", "b", 999)};
    TerritoryResult territory = twoFactions(3);
    territory.strength[0] = {500, 0, 0};
    territory.strength[1] = {0, 500, 0};
    TensionInputs in;
    in.hostility = &matrix;
    in.territory = &territory;
    CHECK(collectTensions(in, config(1, 8)).empty());
}

TEST_CASE("one dominant tension is nearly always kept, faint ones are not", "[tension]")
{
    std::vector<HostilityEntry> matrix = {hostile("boss", "x", kMaxHostility)};
    for (int i = 0; i < 40; ++i) {
        matrix.push_back(hostile("f" + std::to_string(100 + i), "y", kHostileAtLeast));
    }
    // Make the dominant tension dominate by pressure: faint ones are scaled down by data, not config.
    TensionInputs in;
    in.hostility = &matrix;
    std::size_t bossKept = 0;
    std::size_t faintKept = 0;
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        const auto list = sampleTensions(in, config(seed, 4));
        REQUIRE(list.size() == 4);
        bossKept += countKept(list, "boss");
        faintKept += countKept(list, "f100");
    }
    // boss holds 2000/(2000+40*1000) of the weight per draw: far above a faint tension, which is
    // what unweighted sampling (4/41 each) would not show.
    CHECK(bossKept > 2 * faintKept);
    CHECK(bossKept > 20);
}

TEST_CASE("sampling is deterministic per seed, bounded, ordered and varies by seed", "[tension]")
{
    std::vector<HostilityEntry> matrix;
    for (int i = 0; i < 30; ++i) {
        matrix.push_back(hostile("a" + std::to_string(100 + i), "z", kHostileAtLeast + i * 10));
    }
    TensionInputs in;
    in.hostility = &matrix;
    const auto first = sampleTensions(in, config(7, 5));
    const auto again = sampleTensions(in, config(7, 5));
    REQUIRE(first.size() == 5);
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].wanter == again[i].wanter);
        CHECK(first[i].score == again[i].score);
        if (i > 0) {
            CHECK(first[i - 1].score >= first[i].score);
        }
    }
    bool differs = false;
    for (std::uint64_t seed = 8; seed < 40 && !differs; ++seed) {
        const auto other = sampleTensions(in, config(seed, 5));
        differs = other[0].wanter != first[0].wanter || other[4].wanter != first[4].wanter;
    }
    CHECK(differs);
}

TEST_CASE("cap bounds the list and is itself clamped", "[tension]")
{
    std::vector<HostilityEntry> matrix;
    for (int i = 0; i < 200; ++i) {
        matrix.push_back(hostile("a" + std::to_string(1000 + i), "z", kHostileAtLeast));
    }
    TensionInputs in;
    in.hostility = &matrix;
    CHECK(sampleTensions(in, config(1, 0)).empty());
    CHECK(sampleTensions(in, config(1, 3)).size() == 3);
    CHECK(sampleTensions(in, config(1, 1000000)).size() == kMaxTensions);
    CHECK(sampleTensions(in, config(1, std::numeric_limits<std::size_t>::max())).size() == kMaxTensions);
}

TEST_CASE("ties order by kind and id, not by input order", "[tension]")
{
    std::vector<HostilityEntry> forward = {hostile("a", "b", 1200), hostile("c", "d", 1200), hostile("e", "f", 1200)};
    std::vector<HostilityEntry> backward(forward.rbegin(), forward.rend());
    TensionInputs one;
    one.hostility = &forward;
    TensionInputs two;
    two.hostility = &backward;
    const auto x = collectTensions(one, config(1, 8));
    const auto y = collectTensions(two, config(1, 8));
    REQUIRE(x.size() == 3);
    REQUIRE(y.size() == 3);
    CHECK(x[0].wanter == "a");
    CHECK(x[1].wanter == "c");
    CHECK(x[2].wanter == "e");
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(x[i].wanter == y[i].wanter);
    }
}

TEST_CASE("huge inputs stay bounded", "[tension]")
{
    std::vector<HostilityEntry> matrix;
    for (int i = 0; i < 20000; ++i) {
        matrix.push_back(hostile("a" + std::to_string(i), "b" + std::to_string(i), kHostileAtLeast + i % 1000));
    }
    TerritoryResult territory;
    territory.ids.assign(100, "x");
    for (std::size_t i = 0; i < territory.ids.size(); ++i) {
        territory.ids[i] = "f" + std::to_string(i);
    }
    territory.strength.assign(100, std::vector<std::uint16_t>(2000, 65535));
    TensionInputs in;
    in.hostility = &matrix;
    in.territory = &territory;
    const auto all = collectTensions(in, config(1, 8));
    CHECK(all.size() <= kMaxTensionCandidates);
    for (const Tension& t : all) {
        CHECK(t.score >= 1);
        CHECK(t.score <= kMaxTensionScore);
    }
    CHECK(sampleTensions(in, config(1, 64)).size() == 64);
}

TEST_CASE("hostile ids are skipped, not crashed on", "[tension]")
{
    const std::string longId(kMaxTensionIdBytes + 1, 'x');
    std::string binary("a\0b\xff", 4);
    std::vector<HostilityEntry> matrix = {hostile("", "b", 1500), hostile("a", "", 1500), hostile("a", "a", 1500),
        hostile(longId, "b", 1500), hostile("good", binary, 1200), hostile("p", "q", -5), hostile("r", "s", std::numeric_limits<std::int32_t>::max()),
        hostile("t", "u", std::numeric_limits<std::int32_t>::min())};
    TensionInputs in;
    in.hostility = &matrix;
    const auto all = collectTensions(in, config(1, 8));
    REQUIRE(all.size() == 2);
    CHECK(all[0].wanter == "r");
    CHECK(all[0].score == kMaxHostility * 1000);
    CHECK(all[1].wanter == binary);
    CHECK(all[1].holder == "good");

    TerritoryResult ragged;
    ragged.ids = {"a", "", "b"};
    ragged.strength = {{5, 5}, {5}, {5, 5, 5}};
    in.territory = &ragged;
    CHECK(collectTensions(in, config(1, 8)).size() == 3);

    TerritoryResult mismatched;
    mismatched.ids = {"a", "b"};
    mismatched.strength = {{5}};
    in.territory = &mismatched;
    CHECK(collectTensions(in, config(1, 8)).size() == 2);

    ConsequenceLedger ledger;
    REQUIRE(ledger.append(Fact{"f", "s", "k", "", 1}) == AppendResult::Ok);
    in.ledger = &ledger;
    in.ledgerQuery.kinds = {{"k", 1}};
    in.ledgerQuery.horizonTicks = 0;
    in.ledgerQuery.now = std::numeric_limits<std::uint64_t>::max();
    CHECK(collectTensions(in, config(1, 8)).size() >= 2);
}

TEST_CASE("NaN, infinite and negative weights silence a source instead of poisoning scores", "[tension]")
{
    std::vector<HostilityEntry> matrix = {hostile("a", "b", 1500)};
    TerritoryResult territory = twoFactions(1);
    territory.strength[0] = {500};
    territory.strength[1] = {500};
    TensionInputs in;
    in.hostility = &matrix;
    in.territory = &territory;

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    TensionConfig c = config(1, 8);
    c.hostilityWeight = nan;
    auto list = collectTensions(in, c);
    REQUIRE(list.size() == 1);
    CHECK(list[0].kind == TensionKind::Contested);

    c.hostilityWeight = -1.0;
    CHECK(collectTensions(in, c).size() == 1);
    c.hostilityWeight = -inf;
    CHECK(collectTensions(in, c).size() == 1);

    c.hostilityWeight = inf;
    list = collectTensions(in, c);
    REQUIRE(list.size() == 2);
    CHECK(list[0].score == kMaxTensionScore);

    c.hostilityWeight = 1e300;
    CHECK(collectTensions(in, c)[0].score == kMaxTensionScore);

    c.hostilityWeight = nan;
    c.contestedWeight = nan;
    CHECK(sampleTensions(in, c).empty());
}

TEST_CASE("weights shift which source dominates", "[tension]")
{
    std::vector<HostilityEntry> matrix = {hostile("a", "b", 1000)};
    TerritoryResult territory = twoFactions(1);
    territory.strength[0] = {1000};
    territory.strength[1] = {1000};
    TensionInputs in;
    in.hostility = &matrix;
    in.territory = &territory;
    TensionConfig c = config(1, 8);
    CHECK(collectTensions(in, c)[0].kind == TensionKind::Hostility);
    c.hostilityWeight = 0.0005;
    CHECK(collectTensions(in, c)[0].kind == TensionKind::Contested);
}
