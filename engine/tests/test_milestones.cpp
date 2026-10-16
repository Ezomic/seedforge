#include "ichor/progression/milestones.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::vector<Capability> capTable()
{
    return {
        {"pick_locks", "Pick locks", "Open simple locks", {}},
        {"pick_vaults", "Pick vaults", "Open vault locks", {"pick_locks"}},
        {"dive", "Dive", "Hold breath underwater", {}},
    };
}

Milestone lockMilestone()
{
    Milestone m;
    m.id = "thieves_welcome";
    m.quests = {"find_the_key"};
    m.setPieces = {"vault_heist"};
    m.ranks = {{"guild", 2}};
    m.capability = "pick_locks";
    return m;
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

MilestoneFacts allFacts()
{
    MilestoneFacts facts;
    facts.quests.insert("find_the_key");
    facts.setPieces.insert("vault_heist");
    facts.ranks["guild"] = 2;
    return facts;
}

}

TEST_CASE("a valid table has no problems")
{
    std::vector<std::string> problems;
    CHECK(validateMilestones({lockMilestone()}, capTable(), problems));
    CHECK(problems.empty());
}

TEST_CASE("validation rejects bad milestones")
{
    const auto bad = [](Milestone m, const std::string& needle) {
        std::vector<std::string> problems;
        CHECK_FALSE(validateMilestones({m}, capTable(), problems));
        CHECK(mentions(problems, needle));
    };
    Milestone m = lockMilestone();
    m.capability = "nope";
    bad(m, "unknown capability");
    m = lockMilestone();
    m.stat = "grip";
    bad(m, "exactly one");
    m = lockMilestone();
    m.capability.clear();
    bad(m, "exactly one");
    m = lockMilestone();
    m.quests.clear();
    m.setPieces.clear();
    m.ranks.clear();
    bad(m, "at least one condition");
    m = lockMilestone();
    m.id = "Bad Id";
    bad(m, ".id");
    m = lockMilestone();
    m.id = std::string(500, 'a');
    bad(m, ".id");
    m = lockMilestone();
    m.quests = {"../etc/passwd"};
    bad(m, "quest");
    m = lockMilestone();
    m.ranks = {{"guild", -1}};
    bad(m, "rank");
    m.ranks = {{"guild", 0}};
    bad(m, "rank");
    m = lockMilestone();
    m.ranks = {{"guild", 1}, {"guild", 2}};
    bad(m, "duplicate");
    m = lockMilestone();
    m.quests = {"a", "a"};
    bad(m, "duplicate");
}

TEST_CASE("duplicate milestone ids are rejected")
{
    std::vector<std::string> problems;
    CHECK_FALSE(validateMilestones({lockMilestone(), lockMilestone()}, capTable(), problems));
    CHECK(mentions(problems, "duplicate"));
}

TEST_CASE("too many milestones are rejected without work")
{
    std::vector<Milestone> many(5000, lockMilestone());
    std::vector<std::string> problems;
    CHECK_FALSE(validateMilestones(many, capTable(), problems));
    CHECK(problems.size() < 10);
}

TEST_CASE("partial conditions grant nothing")
{
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({lockMilestone()});
    MilestoneFacts facts;
    CHECK(tracker.evaluate(facts, unlocks).empty());
    facts.quests.insert("find_the_key");
    facts.setPieces.insert("vault_heist");
    facts.ranks["guild"] = 1;
    CHECK(tracker.evaluate(facts, unlocks).empty());
    CHECK_FALSE(unlocks.can("pick_locks"));
    facts.ranks["guild"] = 2;
    CHECK(tracker.evaluate(facts, unlocks) == std::vector<std::string>{"thieves_welcome"});
    CHECK(unlocks.can("pick_locks"));
}

TEST_CASE("a higher rank still satisfies the condition")
{
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({lockMilestone()});
    MilestoneFacts facts = allFacts();
    facts.ranks["guild"] = 9;
    CHECK(tracker.evaluate(facts, unlocks).size() == 1);
}

TEST_CASE("a milestone grants exactly once")
{
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({lockMilestone()});
    const MilestoneFacts facts = allFacts();
    CHECK(tracker.evaluate(facts, unlocks).size() == 1);
    CHECK(tracker.evaluate(facts, unlocks).empty());
    CHECK(tracker.granted() == std::vector<std::string>{"thieves_welcome"});
}

TEST_CASE("a capability already learned elsewhere still marks the milestone granted")
{
    CapabilityUnlocks unlocks(capTable());
    REQUIRE(unlocks.unlock("pick_locks") == UnlockResult::Unlocked);
    MilestoneTracker tracker({lockMilestone()});
    CHECK(tracker.evaluate(allFacts(), unlocks).size() == 1);
    CHECK(tracker.isGranted("thieves_welcome"));
}

TEST_CASE("an unmet capability prerequisite defers the grant instead of losing it")
{
    Milestone m = lockMilestone();
    m.id = "vault_master";
    m.capability = "pick_vaults";
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({m});
    CHECK(tracker.evaluate(allFacts(), unlocks).empty());
    CHECK_FALSE(tracker.isGranted("vault_master"));
    REQUIRE(unlocks.unlock("pick_locks") == UnlockResult::Unlocked);
    CHECK(tracker.evaluate(allFacts(), unlocks) == std::vector<std::string>{"vault_master"});
    CHECK(unlocks.can("pick_vaults"));
}

TEST_CASE("stat unlocks are recorded and listed sorted")
{
    Milestone a = lockMilestone();
    a.id = "b_stat";
    a.capability.clear();
    a.stat = "grip";
    Milestone b = a;
    b.id = "a_stat";
    b.stat = "balance";
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({a, b});
    CHECK(tracker.evaluate(allFacts(), unlocks) == std::vector<std::string>{"a_stat", "b_stat"});
    CHECK(tracker.statUnlocks() == std::vector<std::string>{"balance", "grip"});
    CHECK(tracker.hasStat("grip"));
    CHECK_FALSE(tracker.hasStat("nope"));
}

TEST_CASE("empty input is harmless")
{
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({});
    CHECK(tracker.evaluate({}, unlocks).empty());
    CHECK(tracker.granted().empty());
    CHECK_FALSE(tracker.isGranted(""));
    CHECK_FALSE(tracker.hasStat(""));
}

TEST_CASE("a milestone naming an unknown capability is skipped, not crashed on")
{
    Milestone m = lockMilestone();
    m.capability = "ghost";
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({m});
    CHECK(tracker.evaluate(allFacts(), unlocks).empty());
    CHECK_FALSE(tracker.isGranted("thieves_welcome"));
}

TEST_CASE("hostile fact ids never match or crash")
{
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({lockMilestone()});
    MilestoneFacts facts;
    facts.quests.insert(std::string(100000, 'x'));
    facts.quests.insert(std::string("find_the_key\0", 13));
    facts.setPieces.insert("");
    facts.ranks[""] = 1000000;
    CHECK(tracker.evaluate(facts, unlocks).empty());
}

TEST_CASE("grant order is by milestone id, not table order")
{
    Milestone a = lockMilestone();
    a.id = "zeta";
    a.capability = "dive";
    Milestone b = lockMilestone();
    b.id = "alpha";
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({a, b});
    CHECK(tracker.evaluate(allFacts(), unlocks) == std::vector<std::string>{"alpha", "zeta"});
}

TEST_CASE("the gate is load bearing: no facts, no grant")
{
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({lockMilestone()});
    CHECK(tracker.evaluate({}, unlocks).empty());
    CHECK_FALSE(unlocks.can("pick_locks"));
    CHECK(tracker.evaluate(allFacts(), unlocks).size() == 1);
    CHECK(unlocks.can("pick_locks"));
}

TEST_CASE("one evaluation chains a prerequisite granted later in id order")
{
    Milestone vaults = lockMilestone();
    vaults.id = "alpha_vaults";
    vaults.capability = "pick_vaults";
    Milestone locks = lockMilestone();
    locks.id = "zeta_locks";
    CapabilityUnlocks unlocks(capTable());
    MilestoneTracker tracker({vaults, locks});
    CHECK(tracker.evaluate(allFacts(), unlocks) == std::vector<std::string>{"zeta_locks", "alpha_vaults"});
    CHECK(unlocks.can("pick_vaults"));
}
