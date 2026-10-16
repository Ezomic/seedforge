#include "ichor/ledger/ledger_budget.h"
#include "ichor/world/encounter_budget.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

void add(ConsequenceLedger& ledger, const std::string& id, const std::string& subject, const std::string& kind, const std::string& value, std::uint64_t tick)
{
    REQUIRE(ledger.append(Fact{id, subject, kind, value, tick}) == AppendResult::Ok);
}

LedgerBudgetParams params()
{
    LedgerBudgetParams p;
    p.region = "kessen_reach";
    p.faction = "league";
    p.now = 1000;
    return p;
}

ConsequenceLedger escalated(int count)
{
    ConsequenceLedger ledger;
    for (int i = 0; i < count; ++i) {
        add(ledger, "esc" + std::to_string(i), "kessen_reach", "escalation", "1", 990);
        add(ledger, "dead" + std::to_string(i), "kessen_reach", "death", "1", 995);
    }
    add(ledger, "str", "league", "faction.strength", "800", 500);
    return ledger;
}

std::vector<RoleBudget> table()
{
    return {{EnemyRole::Skirmisher, 3, 10, 4}, {EnemyRole::Anchor, 8, 4, 2}, {EnemyRole::Artillery, 5, 6, 3},
        {EnemyRole::Controller, 6, 3, 1}, {EnemyRole::Swarm, 1, 8, 12}, {EnemyRole::Ambusher, 4, 4, 2}};
}

std::vector<Skin> skins()
{
    std::vector<Skin> out;
    for (std::size_t r = 0; r < kEnemyRoleCount; ++r) {
        out.push_back({std::string(enemyRoleName(static_cast<EnemyRole>(r))) + ".base", static_cast<EnemyRole>(r), "", ""});
    }
    return out;
}

double meanSpent(std::uint32_t budget)
{
    std::uint64_t total = 0;
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        std::vector<std::string> problems;
        total += composeEncounter({seed, budget, "league", "marsh"}, table(), skins(), problems).spent;
    }
    return static_cast<double>(total) / 100.0;
}

}

TEST_CASE("an empty ledger gives the base budget", "[ledger_budget]")
{
    const LedgerBudgetParams p = params();
    CHECK(deriveLedgerBudget(ConsequenceLedger{}, p) == p.baseBudget);
}

TEST_CASE("a heavily escalated ledger raises the budget but stays clamped", "[ledger_budget]")
{
    const LedgerBudgetParams p = params();
    const std::uint32_t calm = deriveLedgerBudget(ConsequenceLedger{}, p);
    const std::uint32_t hot = deriveLedgerBudget(escalated(200), p);
    CHECK(hot > calm * 2);
    CHECK(hot <= p.maxBudget);
}

TEST_CASE("the budget is deterministic", "[ledger_budget]")
{
    const ConsequenceLedger ledger = escalated(20);
    CHECK(deriveLedgerBudget(ledger, params()) == deriveLedgerBudget(ledger, params()));
}

TEST_CASE("more escalation never lowers the budget", "[ledger_budget]")
{
    std::uint32_t last = 0;
    for (int n = 0; n <= 400; n += 7) {
        const std::uint32_t b = deriveLedgerBudget(escalated(n), params());
        CHECK(b >= last);
        last = b;
    }
}

TEST_CASE("older escalation counts for less than fresh escalation", "[ledger_budget]")
{
    LedgerBudgetParams p = params();
    p.now = 100000000;
    p.horizonTicks = 1000;
    ConsequenceLedger fresh;
    ConsequenceLedger old;
    for (int i = 0; i < 20; ++i) {
        add(fresh, "e" + std::to_string(i), "kessen_reach", "escalation", "1", p.now - 1);
        add(old, "e" + std::to_string(i), "kessen_reach", "escalation", "1", 0);
    }
    CHECK(deriveLedgerBudget(fresh, p) > deriveLedgerBudget(old, p));
}

TEST_CASE("facts about other regions and unrelated kinds do not count", "[ledger_budget]")
{
    ConsequenceLedger ledger;
    for (int i = 0; i < 50; ++i) {
        add(ledger, "o" + std::to_string(i), "elsewhere", "escalation", "1", 999);
        add(ledger, "k" + std::to_string(i), "kessen_reach", "weather", "1", 999);
    }
    CHECK(deriveLedgerBudget(ledger, params()) == params().baseBudget);
}

TEST_CASE("the ledger input moves the composed encounter", "[ledger_budget]")
{
    const double calm = meanSpent(deriveLedgerBudget(ConsequenceLedger{}, params()));
    const double hot = meanSpent(deriveLedgerBudget(escalated(60), params()));
    CHECK(hot > calm + 10.0);
}

TEST_CASE("faction strength scales the budget, hostile values are ignored", "[ledger_budget]")
{
    const LedgerBudgetParams p = params();
    auto withStrength = [&](const std::string& value) {
        ConsequenceLedger ledger;
        add(ledger, "s", "league", "faction.strength", value, 1);
        return deriveLedgerBudget(ledger, p);
    };
    CHECK(withStrength("1000") > withStrength("500"));
    CHECK(withStrength("500") > withStrength("0"));
    CHECK(withStrength("0") == p.baseBudget);
    for (const char* bad : {"", "-5", "abc", "99999999999999999999999", "1e9", "12 ", "+3", "1001"}) {
        CHECK(withStrength(bad) == p.baseBudget);
    }
}

TEST_CASE("extreme parameters clamp instead of overflowing", "[ledger_budget]")
{
    constexpr std::uint32_t big = std::numeric_limits<std::uint32_t>::max();
    constexpr std::uint64_t huge = std::numeric_limits<std::uint64_t>::max();
    LedgerBudgetParams p = params();
    p.baseBudget = big;
    p.minBudget = big;
    p.maxBudget = big;
    p.maxStrengthBonus = big;
    p.deathWeight = big;
    p.escalationWeight = big;
    p.now = huge;
    p.horizonTicks = huge;
    const std::uint32_t b = deriveLedgerBudget(escalated(300), p);
    CHECK(b <= kMaxLedgerBudget);
    CHECK(b >= 1);

    LedgerBudgetParams q = params();
    q.minBudget = 50;
    q.maxBudget = 5;
    const std::uint32_t inverted = deriveLedgerBudget(ConsequenceLedger{}, q);
    CHECK(inverted >= 1);
    CHECK(inverted <= kMaxLedgerBudget);

    LedgerBudgetParams z = params();
    z.baseBudget = 0;
    z.minBudget = 0;
    CHECK(deriveLedgerBudget(ConsequenceLedger{}, z) >= 1);
}

TEST_CASE("an empty region or faction scores no facts", "[ledger_budget]")
{
    LedgerBudgetParams p = params();
    p.region.clear();
    p.faction.clear();
    CHECK(deriveLedgerBudget(escalated(30), p) == p.baseBudget);
}
