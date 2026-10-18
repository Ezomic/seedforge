#include "ichor/graph/quest_refusal.h"
#include "ichor/data/data_set.h"
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
    static const json j = readJson(kQuestDir + "setpiece_quests_2.json");
    return j;
}

const json& landmarks()
{
    static const json j = readJson(kQuestDir + "setpiece_landmarks_2.json");
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

std::vector<std::string> split(const std::string& text, char sep)
{
    std::vector<std::string> out;
    if (text.empty())
        return out;
    std::size_t from = 0;
    for (;;) {
        const std::size_t at = text.find(sep, from);
        out.push_back(text.substr(from, at == std::string::npos ? at : at - from));
        if (at == std::string::npos)
            return out;
        from = at + 1;
    }
}

std::vector<std::string> fieldsOf(const std::string& record, std::size_t count)
{
    std::vector<std::string> out = split(record, '|');
    out.resize(count);
    return out;
}

BoundQuest bound(const json& q)
{
    BoundQuest out;
    out.id = q.at("id").get<std::string>();
    for (const json& record : q.at("nodes")) {
        const std::vector<std::string> f = fieldsOf(record.get<std::string>(), 6);
        QuestNode node;
        node.id = f[0];
        node.entity = f[1];
        node.cost = static_cast<std::uint32_t>(std::stoul(f[2]));
        node.refusable = f[3] == "1";
        node.after = split(f[4], ',');
        for (const std::string& a : split(f[5], ';')) {
            const std::vector<std::string> parts = split(a, ':');
            REQUIRE(parts.size() == 3);
            node.assumes.push_back({parts[0], parts[1], parts[2]});
        }
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
    for (const json& record : q.at("steps")) {
        const std::vector<std::string> f = fieldsOf(record.get<std::string>(), 3);
        refusal::Step step;
        step.id = static_cast<std::uint32_t>(std::stoul(f[0]));
        step.otherSide = partyNumber(f[1]);
        for (const std::string& option : split(f[2], ';')) {
            const std::vector<std::string> parts = split(option, '>');
            REQUIRE(parts.size() >= 2);
            refusal::Option opt;
            opt.choice = choiceOf(parts[0]);
            opt.next = static_cast<std::uint32_t>(std::stoul(parts[1]));
            opt.consequence = parts.size() > 2 ? parts[2] : "";
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
    for (const json& record : q.at("effects")) {
        const std::vector<std::string> f = fieldsOf(record.get<std::string>(), 3);
        const auto it = metrics.find(f[0]);
        REQUIRE(it != metrics.end());
        out.push_back({it->second, f[1], std::stoll(f[2])});
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

TEST_CASE("set-piece quests 2: second tranche has at least six quests on distinct landmarks", "[quests][setpiece2]")
{
    REQUIRE(quests().size() >= 6);
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

TEST_CASE("set-piece quests 2: every quest passes validation", "[quests][setpiece2]")
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

TEST_CASE("set-piece quests 2: validation goes red on a broken quest", "[quests][setpiece2]")
{
    const auto known = knownEntities();
    const json& q = quests().at(0);
    BoundQuest b = bound(q);

    ConsequenceLedger contradicted = worldMatching(b);
    REQUIRE(contradicted.append({"fact.contradiction", "sunken_amphitheatre", "state", "standing", 2}) == AppendResult::Ok);
    CHECK_FALSE(validateQuest(b, inputsFor(q, contradicted, known)).valid);

    const ConsequenceLedger clean = worldMatching(b);
    QuestValidationInputs tight = inputsFor(q, clean, known);
    tight.budget = 1;
    CHECK_FALSE(validateQuest(b, tight).valid);

    b.nodes.at(0).entity = "not_in_the_world";
    CHECK_FALSE(validateQuest(b, inputsFor(q, clean, known)).valid);
}

TEST_CASE("set-piece quests 2: every quest can be refused at every step", "[quests][setpiece2]")
{
    for (const json& q : quests()) {
        const std::vector<refusal::Finding> findings = refusal::validate(refusalQuest(q));
        INFO(q.at("id").get<std::string>());
        for (const refusal::Finding& f : findings)
            INFO(f.message);
        CHECK(findings.empty());
    }
}

TEST_CASE("set-piece quests 2: refusal check goes red when a walk away is removed", "[quests][setpiece2]")
{
    refusal::Quest q = refusalQuest(quests().at(0));
    auto& options = q.steps.at(1).options;
    std::erase_if(options, [](const refusal::Option& o) { return o.choice == refusal::Choice::WalkAway; });
    CHECK_FALSE(refusal::validate(q).empty());
}

TEST_CASE("set-piece quests 2: every quest moves a visible number", "[quests][setpiece2]")
{
    const auto known = knownEntities();
    for (const json& q : quests()) {
        const VisibleNumberReport report = checkVisibleNumber(effectsOf(q), &known);
        INFO(q.at("id").get<std::string>());
        CHECK(report.verdict == VisibleNumberVerdict::Moves);
    }
}

TEST_CASE("set-piece quests 2: visible number check goes red on cancelling effects", "[quests][setpiece2]")
{
    std::vector<QuestEffect> effects = effectsOf(quests().at(0));
    for (QuestEffect& e : effects)
        e.delta = 0;
    CHECK(checkVisibleNumber(effects).verdict != VisibleNumberVerdict::Moves);
}

TEST_CASE("set-piece quests 2: files load as flat data records", "[quests][setpiece2]")
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_REPO_DATA));
}

TEST_CASE("set-piece quests 2: text has no em-dashes", "[quests][setpiece2]")
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
