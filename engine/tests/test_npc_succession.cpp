#include "ichor/npc/succession.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace ichor;

namespace {

RelationshipEdge edge(const char* a, const char* b, RelationKind kind, int strength = 3)
{
    return {a, b, kind, strength};
}

const std::vector<std::string> kTown = {"brother", "rival", "lover", "stranger", "harbourmaster"};

std::vector<RelationshipEdge> web()
{
    return {edge("brother", "harbourmaster", RelationKind::Family, 2), edge("harbourmaster", "rival", RelationKind::Rival, 5),
        edge("harbourmaster", "lover", RelationKind::Lover, 5)};
}

ConsequenceLedger seeded()
{
    ConsequenceLedger ledger;
    REQUIRE(fillRole(ledger, "harbourmaster", "harbourmaster", 1) == FillResult::Ok);
    return ledger;
}

}

TEST_CASE("a filled role reports its holder")
{
    ConsequenceLedger ledger;
    REQUIRE_FALSE(roleHolder(ledger, "harbourmaster"));
    REQUIRE(fillRole(ledger, "harbourmaster", "ada", 1) == FillResult::Ok);
    REQUIRE(roleHolder(ledger, "harbourmaster") == "ada");
    REQUIRE(fillRole(ledger, "harbourmaster", "bob", 2) == FillResult::RoleTaken);
    REQUIRE(fillRole(ledger, "Harbour", "bob", 2) == FillResult::InvalidKey);
    REQUIRE(ledger.size() == 1);
}

TEST_CASE("family succeeds before a stronger rival")
{
    REQUIRE(chooseSuccessor("harbourmaster", web(), kTown) == "brother");
}

TEST_CASE("the successor choice ignores input order and breaks ties by id")
{
    std::vector<RelationshipEdge> edges = {
        edge("harbourmaster", "zed", RelationKind::Rival, 3), edge("ann", "harbourmaster", RelationKind::Rival, 3)};
    REQUIRE(chooseSuccessor("harbourmaster", edges, {"zed", "ann"}) == "ann");
    std::reverse(edges.begin(), edges.end());
    REQUIRE(chooseSuccessor("harbourmaster", edges, {"ann", "zed"}) == "ann");
}

TEST_CASE("strength decides within one kind and the ineligible are skipped")
{
    const std::vector<RelationshipEdge> edges = {edge("a", "x", RelationKind::Family, 1), edge("a", "y", RelationKind::Family, 4)};
    REQUIRE(chooseSuccessor("a", edges, {"x", "y"}) == "y");
    REQUIRE(chooseSuccessor("a", edges, {"x"}) == "x");
    REQUIRE_FALSE(chooseSuccessor("a", edges, {}));
    REQUIRE_FALSE(chooseSuccessor("a", edges, {"stranger"}));
    REQUIRE_FALSE(chooseSuccessor("a", edges, {"a"}));
}

TEST_CASE("out of range strength cannot beat a better kind")
{
    const std::vector<RelationshipEdge> edges = {edge("a", "x", RelationKind::Family, 1), edge("a", "y", RelationKind::Rival, 1000000)};
    REQUIRE(chooseSuccessor("a", edges, {"x", "y"}) == "x");
}

TEST_CASE("a death hands the role on and the successor remembers why")
{
    ConsequenceLedger ledger = seeded();
    const auto outcome = succeedRole(ledger, "harbourmaster", "harbourmaster", "killed-by-player", 500, web(), kTown);
    REQUIRE(outcome.status == SuccessionStatus::Succeeded);
    REQUIRE(outcome.successor == "brother");
    REQUIRE(roleHolder(ledger, "harbourmaster") == "brother");

    const auto memory = inheritedRoles(ledger, "brother");
    REQUIRE(memory.size() == 1);
    REQUIRE(memory[0].role == "harbourmaster");
    REQUIRE(memory[0].predecessor == "harbourmaster");
    REQUIRE(memory[0].cause == "killed-by-player");
    REQUIRE(memory[0].tick == 500);
    REQUIRE(inheritedRoles(ledger, "rival").empty());
}

TEST_CASE("the succession survives a ledger round trip")
{
    ConsequenceLedger ledger = seeded();
    REQUIRE(succeedRole(ledger, "harbourmaster", "harbourmaster", "plague", 9, web(), kTown).status == SuccessionStatus::Succeeded);
    const auto loaded = ConsequenceLedger::parse(ledger.serialize());
    REQUIRE(loaded);
    REQUIRE(roleHolder(*loaded, "harbourmaster") == "brother");
    REQUIRE(inheritedRoles(*loaded, "brother").size() == 1);
    REQUIRE(loaded->digest() == ledger.digest());
}

TEST_CASE("the same inputs in any order give the same ledger")
{
    ConsequenceLedger a = seeded();
    ConsequenceLedger b = seeded();
    auto reversed = kTown;
    std::reverse(reversed.begin(), reversed.end());
    auto edges = web();
    std::reverse(edges.begin(), edges.end());
    (void)succeedRole(a, "harbourmaster", "harbourmaster", "duel", 7, web(), kTown);
    (void)succeedRole(b, "harbourmaster", "harbourmaster", "duel", 7, edges, reversed);
    REQUIRE(a.size() == 4);
    REQUIRE(a.digest() == b.digest());
}

TEST_CASE("someone who already holds a role is passed over")
{
    ConsequenceLedger ledger = seeded();
    REQUIRE(fillRole(ledger, "smith", "brother", 2) == FillResult::Ok);
    const auto outcome = succeedRole(ledger, "harbourmaster", "harbourmaster", "duel", 10, web(), kTown);
    REQUIRE(outcome.successor == "rival");
    REQUIRE(roleHolder(ledger, "smith") == "brother");
}

TEST_CASE("with nobody to take over the role stays vacant, then can be refilled")
{
    ConsequenceLedger ledger = seeded();
    const auto outcome = succeedRole(ledger, "harbourmaster", "harbourmaster", "duel", 10, {}, kTown);
    REQUIRE(outcome.status == SuccessionStatus::Vacant);
    REQUIRE_FALSE(roleHolder(ledger, "harbourmaster"));
    REQUIRE(ledger.size() == 2);
    REQUIRE(fillRole(ledger, "harbourmaster", "stranger", 20) == FillResult::Ok);
    REQUIRE(roleHolder(ledger, "harbourmaster") == "stranger");
}

TEST_CASE("a succession that cannot happen leaves the ledger alone")
{
    ConsequenceLedger ledger = seeded();
    const auto before = ledger.digest();
    REQUIRE(succeedRole(ledger, "harbourmaster", "brother", "duel", 10, web(), kTown).status == SuccessionStatus::NotHolder);
    REQUIRE(succeedRole(ledger, "nobody", "brother", "duel", 10, web(), kTown).status == SuccessionStatus::NotHolder);
    REQUIRE(succeedRole(ledger, "harbourmaster", "harbourmaster", "Bad Cause", 10, web(), kTown).status == SuccessionStatus::InvalidKey);
    REQUIRE(succeedRole(ledger, "harbourmaster", "harbourmaster", std::string(500, 'a'), 10, web(), kTown).status
        == SuccessionStatus::InvalidKey);
    REQUIRE(succeedRole(ledger, "", "harbourmaster", "duel", 10, web(), kTown).status == SuccessionStatus::InvalidKey);
    REQUIRE(ledger.digest() == before);
}

TEST_CASE("a clashing fact id is refused without partial writes")
{
    ConsequenceLedger ledger = seeded();
    REQUIRE(ledger.append({"filled:10:harbourmaster:brother", "elsewhere", "note", "", 10}) == AppendResult::Ok);
    const auto before = ledger.digest();
    REQUIRE(succeedRole(ledger, "harbourmaster", "harbourmaster", "duel", 10, web(), kTown).status == SuccessionStatus::DuplicateId);
    REQUIRE(ledger.digest() == before);
}

TEST_CASE("a chain of deaths passes the role down the web")
{
    ConsequenceLedger ledger = seeded();
    std::vector<std::string> living = kTown;
    std::string dead = "harbourmaster";
    std::vector<std::string> holders;
    for (std::uint64_t tick = 10; tick < 14; ++tick) {
        const auto outcome = succeedRole(ledger, "harbourmaster", dead, "old-age", tick, web(), living);
        if (outcome.status != SuccessionStatus::Succeeded) break;
        living.erase(std::remove(living.begin(), living.end(), dead), living.end());
        holders.push_back(outcome.successor);
        dead = outcome.successor;
    }
    REQUIRE(holders.size() == 1); // only the brother is linked to the harbourmaster, and he has no web of his own
    REQUIRE(holders[0] == "brother");
    REQUIRE_FALSE(roleHolder(ledger, "harbourmaster"));
}

TEST_CASE("hostile ledger values do not break the memory read")
{
    ConsequenceLedger ledger;
    REQUIRE(ledger.append({"x1", "ann", "inherited-role", ";;;;", 1}) == AppendResult::Ok);
    REQUIRE(ledger.append({"x2", "ann", "inherited-role", "", 2}) == AppendResult::Ok);
    REQUIRE(ledger.append({"x3", "ann", "inherited-role", ";;", 3}) == AppendResult::Ok);
    REQUIRE(inheritedRoles(ledger, "ann").empty());
}

TEST_CASE("a holder who dies in the tick they were installed is still succeeded")
{
    ConsequenceLedger ledger;
    REQUIRE(fillRole(ledger, "harbourmaster", "harbourmaster", 50) == FillResult::Ok);
    REQUIRE(succeedRole(ledger, "harbourmaster", "harbourmaster", "duel", 50, web(), kTown).status == SuccessionStatus::Succeeded);
    REQUIRE(roleHolder(ledger, "harbourmaster") == "brother");
}
