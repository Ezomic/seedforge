#include "ichor/graph/quest_refusal.h"
#include "ichor/ledger/consequence_ledger.h"
#include "ichor/ledger/quest_validation.h"
#include "ichor/ledger/quest_visible_effect.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace ichor;
using Json = nlohmann::json;
namespace refusal = ichor::graph::questrefusal;

namespace {

using Names = std::set<std::string, std::less<>>;
using Cast = std::map<std::string, std::string, std::less<>>;

Json readJson(const std::string& relative)
{
    std::ifstream file(std::string(ICHOR_REPO_DATA) + "/" + relative, std::ios::binary);
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    return Json::parse(text.str());
}

const Json& questlines()
{
    static const Json data = readJson("quests/faction_questlines.json");
    return data;
}

Names factionIds()
{
    Names ids;
    for (const char* file : {"challengers", "custodians", "parasites", "refuseniks"}) {
        for (const Json& f : readJson(std::string("factions/") + file + ".json")) {
            ids.insert(f.at("id").get<std::string>());
        }
    }
    return ids;
}

// Authored quests name cast slots; a bound world fills them. The questline's own
// faction is always the "faction" slot.
Cast castFor(const Json& quest)
{
    return {{"faction", quest.at("faction").get<std::string>()}, {"rival", "rival_faction"}, {"patron", "npc_patron"},
        {"site", "site_shrine"}, {"road", "road_coast"}};
}

Names known(const Cast& cast)
{
    Names names;
    for (const auto& [slot, entity] : cast) {
        names.insert(entity);
    }
    return names;
}

BoundQuest bound(const Json& quest, const Cast& cast)
{
    BoundQuest out;
    out.id = quest.at("id").get<std::string>();
    for (const Json& s : quest.at("steps")) {
        QuestNode n;
        n.id = s.at("id").get<std::string>();
        n.entity = cast.at(s.at("slot").get<std::string>());
        n.cost = s.at("cost").get<std::uint32_t>();
        n.refusable = s.at("refusable").get<bool>();
        n.after = s.at("after").get<std::vector<std::string>>();
        for (const Json& a : s.value("assumes", Json::array())) {
            n.assumes.push_back({cast.at(a.at("slot").get<std::string>()), a.at("kind").get<std::string>(), a.at("value").get<std::string>()});
        }
        out.nodes.push_back(std::move(n));
    }
    return out;
}

ConsequenceLedger ledgerFor(const Json& quest, const Cast& cast, bool contradict)
{
    ConsequenceLedger ledger;
    int n = 0;
    for (const Json& s : quest.at("steps")) {
        for (const Json& a : s.value("assumes", Json::array())) {
            Fact f;
            f.id = "f" + std::to_string(++n);
            f.subject = cast.at(a.at("slot").get<std::string>());
            f.kind = a.at("kind").get<std::string>();
            f.value = contradict ? "other" : a.at("value").get<std::string>();
            f.tick = static_cast<std::uint64_t>(n);
            REQUIRE(ledger.append(f) == AppendResult::Ok);
        }
    }
    return ledger;
}

refusal::Quest refusalShape(const Json& quest, bool dropWalkAway = false)
{
    refusal::Quest out;
    const Json& steps = quest.at("steps");
    out.start = 1;
    std::uint32_t id = 0;
    for (const Json& s : steps) {
        refusal::Step step;
        step.id = ++id;
        if (s.contains("other_side")) {
            step.otherSide = 1000 + id;
        }
        const bool last = id == steps.size();
        step.options.push_back({refusal::Choice::Accept, last ? 0 : id + 1, ""});
        if (id == 1) {
            step.options.push_back({refusal::Choice::Decline, 0, s.at("decline").get<std::string>()});
        } else if (!dropWalkAway) {
            step.options.push_back({refusal::Choice::WalkAway, 0, s.at("walk_away").get<std::string>()});
        }
        if (s.contains("tell")) {
            step.options.push_back({refusal::Choice::TellOtherSide, 0, s.at("tell").get<std::string>()});
        }
        out.steps.push_back(std::move(step));
    }
    return out;
}

std::vector<QuestEffect> effectsFor(const Json& quest, const Cast& cast)
{
    static const std::map<std::string, VisibleMetric, std::less<>> metrics = {{"territory", VisibleMetric::Territory},
        {"supply_status", VisibleMetric::SupplyStatus}, {"faction_strength", VisibleMetric::FactionStrength}, {"route", VisibleMetric::Route}};
    std::vector<QuestEffect> out;
    for (const Json& e : quest.at("effects")) {
        out.push_back({metrics.at(e.at("metric").get<std::string>()), cast.at(e.at("slot").get<std::string>()), e.at("delta").get<std::int64_t>()});
    }
    return out;
}

void collectStrings(const Json& j, std::vector<std::string>& out)
{
    if (j.is_string()) {
        out.push_back(j.get<std::string>());
    } else if (j.is_array() || j.is_object()) {
        for (const Json& child : j) {
            collectStrings(child, out);
        }
    }
}

}

TEST_CASE("faction questlines cover at least three real factions with several quests each", "[faction_questlines]")
{
    const Names factions = factionIds();
    std::map<std::string, int, std::less<>> perFaction;
    Names ids;
    for (const Json& q : questlines()) {
        const std::string faction = q.at("faction").get<std::string>();
        CHECK(factions.count(faction) == 1);
        CHECK(ids.insert(q.at("id").get<std::string>()).second);
        ++perFaction[faction];
    }
    CHECK(perFaction.size() >= 3);
    for (const auto& [faction, count] : perFaction) {
        CHECK(count >= 3);
    }
}

TEST_CASE("every faction quest passes validation, refusal and visible number checks", "[faction_questlines]")
{
    REQUIRE_FALSE(questlines().empty());
    for (const Json& q : questlines()) {
        INFO(q.at("id").get<std::string>());
        const Cast cast = castFor(q);
        const Names entities = known(cast);
        const ConsequenceLedger ledger = ledgerFor(q, cast, false);

        QuestValidationInputs in;
        in.ledger = &ledger;
        in.knownEntities = &entities;
        in.budget = q.at("budget").get<std::uint64_t>();
        const QuestVerdict verdict = validateQuest(bound(q, cast), in);
        CHECK(verdict.issues.empty());
        CHECK(verdict.valid);

        CHECK(refusal::validate(refusalShape(q)).empty());

        const VisibleNumberReport report = checkVisibleNumber(effectsFor(q, cast), &entities);
        CHECK(report.verdict == VisibleNumberVerdict::Moves);
    }
}

TEST_CASE("the checks go red when a quest is broken", "[faction_questlines]")
{
    bool sawAssumption = false;
    for (const Json& q : questlines()) {
        INFO(q.at("id").get<std::string>());
        const Cast cast = castFor(q);
        const Names entities = known(cast);

        CHECK_FALSE(refusal::validate(refusalShape(q, true)).empty());

        std::vector<QuestEffect> cancelled = effectsFor(q, cast);
        const std::vector<QuestEffect> original = cancelled;
        for (QuestEffect e : original) {
            e.delta = -e.delta;
            cancelled.push_back(e);
        }
        CHECK(checkVisibleNumber(cancelled, &entities).verdict == VisibleNumberVerdict::AllCancel);

        QuestValidationInputs tight;
        tight.knownEntities = &entities;
        tight.budget = 1;
        CHECK_FALSE(validateQuest(bound(q, cast), tight).valid);

        const ConsequenceLedger hostile = ledgerFor(q, cast, true);
        QuestValidationInputs in;
        in.ledger = &hostile;
        in.knownEntities = &entities;
        in.budget = q.at("budget").get<std::uint64_t>();
        if (!hostile.empty()) {
            sawAssumption = true;
            CHECK_FALSE(validateQuest(bound(q, cast), in).valid);
        }
    }
    CHECK(sawAssumption);
}

TEST_CASE("faction quests use only cast slots and carry a refusal path on every step", "[faction_questlines]")
{
    for (const Json& q : questlines()) {
        INFO(q.at("id").get<std::string>());
        const Cast cast = castFor(q);
        bool first = true;
        for (const Json& s : q.at("steps")) {
            CHECK(cast.count(s.at("slot").get<std::string>()) == 1);
            CHECK(s.contains(first ? "decline" : "walk_away"));
            first = false;
        }
        for (const Json& e : q.at("effects")) {
            CHECK(cast.count(e.at("slot").get<std::string>()) == 1);
        }
    }
}

TEST_CASE("faction quest text has no em-dashes", "[faction_questlines]")
{
    std::vector<std::string> strings;
    collectStrings(questlines(), strings);
    REQUIRE_FALSE(strings.empty());
    for (const std::string& s : strings) {
        CHECK(s.find("\xE2\x80\x94") == std::string::npos);
        CHECK(s.find("--") == std::string::npos);
    }
}
