#include "ichor/ledger/quest_validation.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <string>

using namespace ichor;

namespace {

using Entities = std::set<std::string, std::less<>>;

QuestNode node(const std::string& id, const std::string& entity, std::uint32_t cost, bool refusable, std::vector<std::string> after = {})
{
    QuestNode n;
    n.id = id;
    n.entity = entity;
    n.cost = cost;
    n.refusable = refusable;
    n.after = std::move(after);
    return n;
}

BoundQuest goodQuest()
{
    BoundQuest q;
    q.id = "ordmoor";
    q.nodes.push_back(node("a", "telestai", 3, false));
    q.nodes.push_back(node("b", "league", 4, true, {"a"}));
    q.nodes.push_back(node("c", "kessene", 2, false, {"a", "b"}));
    return q;
}

Entities entities()
{
    return {"telestai", "league", "kessene"};
}

bool has(const QuestVerdict& v, QuestIssueKind kind)
{
    return std::any_of(v.issues.begin(), v.issues.end(), [&](const QuestIssue& i) { return i.kind == kind; });
}

QuestVerdict run(const BoundQuest& q, const ConsequenceLedger* ledger, const Entities& known, std::uint64_t budget)
{
    QuestValidationInputs in;
    in.ledger = ledger;
    in.knownEntities = &known;
    in.budget = budget;
    return validateQuest(q, in);
}

}

TEST_CASE("a well formed quest passes", "[quest_validation]")
{
    const auto known = entities();
    const QuestVerdict v = run(goodQuest(), nullptr, known, 20);
    CHECK(v.valid);
    CHECK(v.issues.empty());
    CHECK(v.totalCost == 9);
}

TEST_CASE("empty quest fails", "[quest_validation]")
{
    const auto known = entities();
    const QuestVerdict v = run(BoundQuest{}, nullptr, known, 20);
    CHECK_FALSE(v.valid);
    CHECK(has(v, QuestIssueKind::Empty));
}

TEST_CASE("no refusable node fails", "[quest_validation]")
{
    auto q = goodQuest();
    q.nodes[1].refusable = false;
    const auto known = entities();
    const QuestVerdict v = run(q, nullptr, known, 20);
    CHECK_FALSE(v.valid);
    CHECK(has(v, QuestIssueKind::NoRefusableNode));
}

TEST_CASE("budget is a hard ceiling and exact budget passes", "[quest_validation]")
{
    const auto known = entities();
    CHECK(run(goodQuest(), nullptr, known, 9).valid);
    const QuestVerdict v = run(goodQuest(), nullptr, known, 8);
    CHECK_FALSE(v.valid);
    CHECK(has(v, QuestIssueKind::OverBudget));
}

TEST_CASE("extreme costs do not overflow the total", "[quest_validation]")
{
    auto q = goodQuest();
    for (auto& n : q.nodes) {
        n.cost = std::numeric_limits<std::uint32_t>::max();
    }
    const auto known = entities();
    const QuestVerdict v = run(q, nullptr, known, std::numeric_limits<std::uint64_t>::max());
    CHECK(v.valid);
    CHECK(v.totalCost == 3ull * std::numeric_limits<std::uint32_t>::max());
    CHECK(has(run(q, nullptr, known, 1), QuestIssueKind::OverBudget));
}

TEST_CASE("unbound and unknown entities fail", "[quest_validation]")
{
    auto q = goodQuest();
    q.nodes[0].entity = "";
    q.nodes[2].entity = "nobody";
    const auto known = entities();
    const QuestVerdict v = run(q, nullptr, known, 20);
    CHECK(std::count_if(v.issues.begin(), v.issues.end(), [](const QuestIssue& i) { return i.kind == QuestIssueKind::UnboundEntity; }) == 2);
    CHECK_FALSE(v.valid);
}

TEST_CASE("null known entity set rejects every binding", "[quest_validation]")
{
    QuestValidationInputs in;
    in.budget = 20;
    const QuestVerdict v = validateQuest(goodQuest(), in);
    CHECK_FALSE(v.valid);
    CHECK(has(v, QuestIssueKind::UnboundEntity));
}

TEST_CASE("cycles and nodes behind them are unreachable", "[quest_validation]")
{
    BoundQuest q;
    q.id = "loop";
    q.nodes.push_back(node("start", "league", 1, true));
    q.nodes.push_back(node("x", "league", 1, false, {"start", "y"}));
    q.nodes.push_back(node("y", "league", 1, false, {"x"}));
    q.nodes.push_back(node("z", "league", 1, false, {"y"}));
    const auto known = entities();
    const QuestVerdict v = run(q, nullptr, known, 20);
    CHECK_FALSE(v.valid);
    int unreachable = 0;
    for (const auto& i : v.issues) {
        if (i.kind == QuestIssueKind::Unreachable) {
            ++unreachable;
        }
    }
    CHECK(unreachable == 3);
}

TEST_CASE("self dependency is unreachable", "[quest_validation]")
{
    BoundQuest q;
    q.nodes.push_back(node("a", "league", 1, true, {"a"}));
    const auto known = entities();
    CHECK(has(run(q, nullptr, known, 5), QuestIssueKind::Unreachable));
}

TEST_CASE("quest with no starting node is unreachable", "[quest_validation]")
{
    BoundQuest q;
    q.nodes.push_back(node("a", "league", 1, true, {"b"}));
    q.nodes.push_back(node("b", "league", 1, false, {"a"}));
    const auto known = entities();
    CHECK(has(run(q, nullptr, known, 5), QuestIssueKind::Unreachable));
}

TEST_CASE("unknown dependency and duplicate ids fail", "[quest_validation]")
{
    auto q = goodQuest();
    q.nodes[2].after.push_back("ghost");
    q.nodes.push_back(node("a", "league", 1, false));
    const auto known = entities();
    const QuestVerdict v = run(q, nullptr, known, 50);
    CHECK_FALSE(v.valid);
    CHECK(has(v, QuestIssueKind::UnknownDependency));
    CHECK(has(v, QuestIssueKind::DuplicateNode));
}

TEST_CASE("an assumption the latest fact contradicts fails", "[quest_validation]")
{
    ConsequenceLedger ledger;
    REQUIRE(ledger.append(Fact{"f1", "kessene", "starving", "false", 1}) == AppendResult::Ok);
    REQUIRE(ledger.append(Fact{"f2", "kessene", "starving", "true", 5}) == AppendResult::Ok);

    auto q = goodQuest();
    q.nodes[2].assumes.push_back({"kessene", "starving", "false"});
    const auto known = entities();
    const QuestVerdict v = run(q, &ledger, known, 20);
    CHECK_FALSE(v.valid);
    CHECK(has(v, QuestIssueKind::LedgerContradiction));

    q.nodes[2].assumes[0].value = "true";
    CHECK(run(q, &ledger, known, 20).valid);
}

TEST_CASE("an assumption with no matching fact is not a contradiction", "[quest_validation]")
{
    ConsequenceLedger ledger;
    auto q = goodQuest();
    q.nodes[0].assumes.push_back({"kessene", "starving", "true"});
    const auto known = entities();
    CHECK(run(q, &ledger, known, 20).valid);
    CHECK(run(q, nullptr, known, 20).valid);
}

TEST_CASE("size limits reject without scanning", "[quest_validation]")
{
    BoundQuest q;
    for (std::size_t i = 0; i <= kMaxQuestNodes; ++i) {
        q.nodes.push_back(node("n" + std::to_string(i), "league", 0, true));
    }
    const auto known = entities();
    const QuestVerdict v = run(q, nullptr, known, 20);
    CHECK_FALSE(v.valid);
    CHECK(has(v, QuestIssueKind::TooLarge));
}

TEST_CASE("verdict is deterministic and ordered by check", "[quest_validation]")
{
    auto q = goodQuest();
    q.nodes[1].refusable = false;
    q.nodes[0].entity = "nobody";
    const auto known = entities();
    const QuestVerdict a = run(q, nullptr, known, 1);
    const QuestVerdict b = run(q, nullptr, known, 1);
    REQUIRE(a.issues.size() == b.issues.size());
    REQUIRE(a.issues.size() >= 3);
    for (std::size_t i = 0; i < a.issues.size(); ++i) {
        CHECK(a.issues[i].kind == b.issues[i].kind);
        CHECK(a.issues[i].node == b.issues[i].node);
    }
    for (std::size_t i = 1; i < a.issues.size(); ++i) {
        CHECK(a.issues[i - 1].kind <= a.issues[i].kind);
    }
}
