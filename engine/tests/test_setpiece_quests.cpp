#include "ichor/graph/quest_refusal.h"
#include "ichor/ledger/consequence_ledger.h"
#include "ichor/ledger/quest_validation.h"
#include "ichor/ledger/quest_visible_effect.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ichor;
namespace refusal = ichor::graph::questrefusal;
using nlohmann::json;

namespace {

json readJson(const std::string& path)
{
    std::ifstream in(path);
    REQUIRE(in.good());
    return json::parse(in);
}

const std::string kQuestDir = std::string(ICHOR_REPO_DATA) + "/quests/";

const json& quests()
{
    static const json j = readJson(kQuestDir + "setpiece_quests.json");
    return j;
}

const json& landmarks()
{
    static const json j = readJson(kQuestDir + "setpiece_landmarks.json");
    return j;
}

std::set<std::string, std::less<>> knownEntities()
{
    std::set<std::string, std::less<>> out;
    for (const char* file : {"challengers", "custodians", "parasites", "refuseniks"})
        for (const json& f : readJson(std::string(ICHOR_REPO_DATA) + "/factions/" + file + ".json"))
            out.insert(f.at("id").get<std::string>());
    for (const json& l : landmarks()) {
        out.insert(l.at("id").get<std::string>());
        for (const json& r : l.at("roles"))
            out.insert(r.get<std::string>());
        for (const json& r : l.at("routes"))
            out.insert(r.get<std::string>());
    }
    return out;
}

BoundQuest bound(const json& q)
{
    BoundQuest out;
    out.id = q.at("id").get<std::string>();
    for (const json& n : q.at("nodes")) {
        QuestNode node;
        node.id = n.at("id").get<std::string>();
        node.entity = n.at("entity").get<std::string>();
        node.cost = n.at("cost").get<std::uint32_t>();
        node.refusable = n.at("refusable").get<bool>();
        node.after = n.at("after").get<std::vector<std::string>>();
        for (const json& a : n.at("assumes"))
            node.assumes.push_back({a.at("subject").get<std::string>(), a.at("kind").get<std::string>(), a.at("value").get<std::string>()});
        out.nodes.push_back(std::move(node));
    }
    return out;
}

ConsequenceLedger worldMatching(const BoundQuest& q)
{
    ConsequenceLedger ledger;
    int n = 0;
    for (const QuestNode& node : q.nodes)
        for (const QuestAssumption& a : node.assumes)
            REQUIRE(ledger.append({"fact." + q.id + "." + std::to_string(n++), a.subject, a.kind, a.value, 1}) == AppendResult::Ok);
    return ledger;
}

std::uint32_t partyNumber(const std::string& party)
{
    static std::map<std::string, std::uint32_t> ids;
    if (party.empty())
        return 0;
    return ids.emplace(party, static_cast<std::uint32_t>(ids.size() + 1)).first->second;
}

refusal::Choice choiceOf(const std::string& c)
{
    if (c == "Accept")
        return refusal::Choice::Accept;
    if (c == "Decline")
        return refusal::Choice::Decline;
    if (c == "WalkAway")
        return refusal::Choice::WalkAway;
    REQUIRE(c == "TellOtherSide");
    return refusal::Choice::TellOtherSide;
}

refusal::Quest refusalQuest(const json& q)
{
    refusal::Quest out;
    out.start = q.at("start").get<std::uint32_t>();
    for (const json& s : q.at("steps")) {
        refusal::Step step;
        step.id = s.at("id").get<std::uint32_t>();
        step.otherSide = partyNumber(s.at("other_side").get<std::string>());
        for (const json& o : s.at("options")) {
            refusal::Option opt;
            opt.choice = choiceOf(o.at("choice").get<std::string>());
            opt.next = o.at("next").get<std::uint32_t>();
            opt.consequence = o.value("consequence", "");
            step.options.push_back(std::move(opt));
        }
        out.steps.push_back(std::move(step));
    }
    return out;
}

std::vector<QuestEffect> effectsOf(const json& q)
{
    static const std::map<std::string, VisibleMetric> metrics = {{"Territory", VisibleMetric::Territory},
        {"SupplyStatus", VisibleMetric::SupplyStatus}, {"FactionStrength", VisibleMetric::FactionStrength}, {"Route", VisibleMetric::Route}};
    std::vector<QuestEffect> out;
    for (const json& e : q.at("effects")) {
        const auto it = metrics.find(e.at("metric").get<std::string>());
        REQUIRE(it != metrics.end());
        out.push_back({it->second, e.at("target").get<std::string>(), e.at("delta").get<std::int64_t>()});
    }
    return out;
}

void collectStrings(const json& j, std::vector<std::string>& out)
{
    if (j.is_string())
        out.push_back(j.get<std::string>());
    else if (j.is_array() || j.is_object())
        for (const json& c : j)
            collectStrings(c, out);
}

QuestValidationInputs inputsFor(const json& q, const ConsequenceLedger& ledger, const std::set<std::string, std::less<>>& known)
{
    QuestValidationInputs in;
    in.ledger = &ledger;
    in.knownEntities = &known;
    in.budget = q.at("budget").get<std::uint64_t>();
    return in;
}

} // namespace

TEST_CASE("set-piece quests: first tranche has at least five quests on distinct landmarks", "[quests][setpiece]")
{
    REQUIRE(quests().size() >= 5);
    std::set<std::string> archetypes;
    std::set<std::string> ids;
    std::set<std::string> landmarkIds;
    for (const json& l : landmarks())
        landmarkIds.insert(l.at("id").get<std::string>());
    for (const json& q : quests()) {
        CHECK(ids.insert(q.at("id").get<std::string>()).second);
        CHECK(archetypes.insert(q.at("archetype").get<std::string>()).second);
        CHECK(landmarkIds.contains(q.at("archetype").get<std::string>()));
    }
}

TEST_CASE("set-piece quests: every quest passes validation", "[quests][setpiece]")
{
    const auto known = knownEntities();
    for (const json& q : quests()) {
        const BoundQuest b = bound(q);
        const ConsequenceLedger ledger = worldMatching(b);
        const QuestVerdict v = validateQuest(b, inputsFor(q, ledger, known));
        INFO(b.id);
        for (const QuestIssue& issue : v.issues)
            INFO(issue.node << ": " << issue.detail);
        CHECK(v.valid);
        CHECK(v.totalCost > 0);
    }
}

TEST_CASE("set-piece quests: validation goes red on a broken quest", "[quests][setpiece]")
{
    const auto known = knownEntities();
    const json& q = quests().at(0);
    BoundQuest b = bound(q);

    ConsequenceLedger contradicted = worldMatching(b);
    REQUIRE(contradicted.append({"fact.contradiction", "fallen_colossus", "state", "standing", 2}) == AppendResult::Ok);
    CHECK_FALSE(validateQuest(b, inputsFor(q, contradicted, known)).valid);

    const ConsequenceLedger clean = worldMatching(b);
    QuestValidationInputs tight = inputsFor(q, clean, known);
    tight.budget = 1;
    CHECK_FALSE(validateQuest(b, tight).valid);

    b.nodes.at(0).entity = "not_in_the_world";
    CHECK_FALSE(validateQuest(b, inputsFor(q, clean, known)).valid);
}

TEST_CASE("set-piece quests: every quest can be refused at every step", "[quests][setpiece]")
{
    for (const json& q : quests()) {
        const std::vector<refusal::Finding> findings = refusal::validate(refusalQuest(q));
        INFO(q.at("id").get<std::string>());
        for (const refusal::Finding& f : findings)
            INFO(f.message);
        CHECK(findings.empty());
    }
}

TEST_CASE("set-piece quests: refusal check goes red when a walk away is removed", "[quests][setpiece]")
{
    refusal::Quest q = refusalQuest(quests().at(0));
    auto& options = q.steps.at(1).options;
    std::erase_if(options, [](const refusal::Option& o) { return o.choice == refusal::Choice::WalkAway; });
    CHECK_FALSE(refusal::validate(q).empty());
}

TEST_CASE("set-piece quests: every quest moves a visible number", "[quests][setpiece]")
{
    const auto known = knownEntities();
    for (const json& q : quests()) {
        const VisibleNumberReport report = checkVisibleNumber(effectsOf(q), &known);
        INFO(q.at("id").get<std::string>());
        CHECK(report.verdict == VisibleNumberVerdict::Moves);
    }
}

TEST_CASE("set-piece quests: visible number check goes red on cancelling effects", "[quests][setpiece]")
{
    std::vector<QuestEffect> effects = effectsOf(quests().at(0));
    for (QuestEffect& e : effects)
        e.delta = 0;
    CHECK(checkVisibleNumber(effects).verdict != VisibleNumberVerdict::Moves);
}

TEST_CASE("set-piece quests: text has no em-dashes", "[quests][setpiece]")
{
    std::vector<std::string> strings;
    collectStrings(quests(), strings);
    collectStrings(landmarks(), strings);
    REQUIRE_FALSE(strings.empty());
    for (const std::string& s : strings) {
        INFO(s);
        CHECK(s.find("\xE2\x80\x94") == std::string::npos);
        CHECK(s.find("--") == std::string::npos);
    }
}
