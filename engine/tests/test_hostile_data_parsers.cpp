#include "hostile_text.h"

#include "ichor/assets/material_limits.h"
#include "ichor/data/axiom.h"
#include "ichor/data/conflicts.h"
#include "ichor/data/featured_seeds.h"
#include "ichor/data/npc_tiers.h"
#include "ichor/data/synergy.h"
#include "ichor/data/ue_export.h"
#include "ichor/data/wants_fears.h"
#include "ichor/factions/challenger_mapping.h"
#include "ichor/factions/custodian_mapping.h"
#include "ichor/factions/faction_palette.h"
#include "ichor/factions/factions.h"
#include "ichor/factions/herald_mapping.h"
#include "ichor/factions/parasite_mapping.h"
#include "ichor/factions/refusenik_mapping.h"
#include "ichor/factions/standing.h"
#include "ichor/graph/mission.h"
#include "ichor/graph/quest_expansion.h"
#include "ichor/npc/dialogue_select.h"
#include "ichor/progression/capabilities.h"
#include "ichor/progression/power_growth.h"
#include "ichor/progression/respec.h"
#include "ichor/progression/trainers.h"
#include "ichor/save/autosave_policy.h"
#include "ichor/ui/hud_layout.h"
#include "ichor/ui/law_hud_config.h"
#include "ichor/world/enemy_reskin.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <cstdint>
#include <string>
#include <vector>

using namespace ichor;

namespace {

namespace fs = std::filesystem;

struct Sample {
    std::string name;
    std::string text;
};

std::string slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Every JSON file under the repo data directory, smallest first, so a parser's valid samples stay cheap to mutate.
const std::vector<Sample>& corpus()
{
    static const std::vector<Sample> files = [] {
        std::vector<Sample> out;
        for (const auto& entry : fs::recursive_directory_iterator(ICHOR_REPO_DATA)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            Sample s{entry.path().generic_string(), slurp(entry.path())};
            if (s.text.size() <= 60000) out.push_back(std::move(s));
        }
        std::sort(out.begin(), out.end(), [](const Sample& a, const Sample& b) { return a.text.size() != b.text.size() ? a.text.size() < b.text.size() : a.name < b.name; });
        return out;
    }();
    return files;
}

const AxiomSet& axioms()
{
    static const AxiomSet set = [] {
        AxiomSet s;
        REQUIRE(s.loadDirectory(ICHOR_REPO_DATA));
        return s;
    }();
    return set;
}

const std::vector<Capability>& capabilities()
{
    static const std::vector<Capability> table = [] {
        std::vector<Capability> out;
        std::vector<std::string> problems;
        REQUIRE(parseCapabilities("c", slurp(fs::path(ICHOR_REPO_DATA) / "progression" / "capabilities.json"), out, problems));
        return out;
    }();
    return table;
}

const std::vector<Faction>& factions()
{
    static const std::vector<Faction> table = {
        {"archonate", "The Archonate", FactionRole::Incumbent},
        {"archaists", "The Archaists", FactionRole::Challenger},
        {"mnemones", "The Mnemones", FactionRole::Custodian},
    };
    return table;
}

const std::string kTiers = R"([
    {"id": "hostile", "name": "Hostile", "threshold": -1000},
    {"id": "wary", "name": "Wary", "threshold": -250},
    {"id": "neutral", "name": "Neutral", "threshold": 0},
    {"id": "friendly", "name": "Friendly", "threshold": 250},
    {"id": "allied", "name": "Allied", "threshold": 750}
])";

const std::string kGates = R"([
    {"id": "safe_passage", "faction": "archonate", "min_tier": "wary"},
    {"id": "trainer_basic", "faction": "archaists", "min_tier": "friendly"},
    {"id": "base_access", "faction": "mnemones", "min_tier": "allied"}
])";

const std::vector<StandingTier>& tiers()
{
    static const std::vector<StandingTier> out = [] {
        std::vector<StandingTier> t;
        std::vector<std::string> problems;
        REQUIRE(parseStandingTiers("t", kTiers, t, problems));
        return t;
    }();
    return out;
}

const char* kUeSchema = R"({"tables":[{"table":"currency","assetClass":"UCurrencyData","fields":[
  {"name":"name","type":"string","required":true},
  {"name":"cap","type":"int32","required":true},
  {"name":"rate","type":"float"},
  {"name":"tag","type":"name"},
  {"name":"hidden","type":"bool"},
  {"name":"earn","type":"string_array"}]}]})";

}

namespace {

using hostile::ParseFn;

// Mutates up to `perParser` samples the parser accepts; fails when none is accepted, so a parser
// cannot pass by rejecting every sample it is given.
void hostileParser(const char* name, const ParseFn& parse, const std::vector<Sample>& extras = {}, std::size_t perParser = 2)
{
    std::vector<Sample> valid;
    for (const Sample& s : extras) {
        if (parse(s.text)) valid.push_back(s);
    }
    for (const Sample& s : corpus()) {
        if (valid.size() >= perParser) break;
        if (parse(s.text)) valid.push_back(s);
    }
    INFO(name);
    REQUIRE_FALSE(valid.empty());
    for (const Sample& s : valid) {
        INFO(name << " on " << s.name);
        hostile::TextOptions options;
        options.randomMutations = 600;
        const hostile::TextReport report = hostile::mutateText(s.text, parse, options);
        INFO(hostile::summary(report));
        CHECK(report.cases > 100);
        CHECK(report.rejected > 0);
        CHECK(report.ok());
    }
}

}

TEST_CASE("hostile: the text mutator reports a parser that trusts a hostile number", "[hostile][harness]")
{
    const std::string valid = R"({"count": 3})";
    const hostile::TextReport report = hostile::mutateText(valid, [](std::string_view text) {
        const std::size_t at = text.find(':');
        if (at == std::string_view::npos) return false;
        std::string digits;
        for (char c : text.substr(at + 1)) {
            if (c >= '0' && c <= '9') digits += c;
        }
        if (digits.empty() || digits.size() > 18) return false;
        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(std::stoull(digits)) << 20);
        return !buffer.empty();
    });
    CHECK_FALSE(report.ok());
}

TEST_CASE("hostile: the text mutator reports a parser that throws", "[hostile][harness]")
{
    const hostile::TextReport report = hostile::mutateText("[1,2,3]", [](std::string_view text) -> bool {
        if (text.size() == 5) throw std::runtime_error("boom");
        return false;
    });
    CHECK_FALSE(report.ok());
}

TEST_CASE("hostile: the corpus holds the repo data", "[hostile][harness]")
{
    CHECK(corpus().size() > 40);
}

#define PROBLEMS_PARSER(fn, Type)                                                       \
    [](std::string_view text) {                                                         \
        std::vector<Type> out;                                                          \
        std::vector<std::string> problems;                                              \
        return fn("hostile.json", text, out, problems);                                 \
    }

TEST_CASE("hostile: axiom", "[hostile][data]")
{
    hostileParser("axiom", [](std::string_view text) {
        Axiom out;
        std::vector<std::string> problems;
        return parseAxiom("hostile.json", text, out, problems);
    });
}

TEST_CASE("hostile: synergies", "[hostile][data]")
{
    const AxiomPool pool = axiomPool(axioms());
    hostileParser("synergies", [&pool](std::string_view text) {
        std::vector<Synergy> out;
        std::vector<std::string> problems;
        return parseSynergies("hostile.json", text, pool, out, problems);
    });
}

TEST_CASE("hostile: conflicts", "[hostile][data]")
{
    hostileParser("conflicts", [](std::string_view text) {
        ConflictList list;
        return list.loadText("hostile.json", text, axioms());
    });
}

TEST_CASE("hostile: npc tiers", "[hostile][data]")
{
    const std::string principal = R"({"id":"auditor","tier":"principal","name":"Teles the Auditor","voice":"auditor_dry","arc":"ledger_truth","vo":"auditor_vo","face":"auditor_face","cast_roles":["faction_leader","ledger_keeper"]})";
    const std::string regular = R"({"id":"ilse","tier":"regular","name":"Ilse Vance","home":"saltmarsh","role":"salt_panner","traits":["bitter","proud"],
  "relationships":[{"kind":"family","target":"harl"},{"kind":"creditor","target":"auditor"},{"kind":"rival","target":"maren"}],
  "want":"Wants her brother off the dive crews.","fear":"Drowning like her father.","memory":"ledger_ilse"})";
    const std::string harl = R"({"id":"harl","tier":"resident","name":"Harl Vance","location":"saltmarsh"})";
    const std::string crowd = R"({"id":"market_murmur","tier":"crowd","ambience":"market_chatter"})";
    hostileParser("npc tiers", PROBLEMS_PARSER(parseNpcFile, NpcRecord), {{"inline", "[" + principal + "," + regular + "," + harl + "," + crowd + "]"}});
}
TEST_CASE("hostile: wants and fears", "[hostile][data]") { hostileParser("desires", PROBLEMS_PARSER(parseDesires, Desire)); }
TEST_CASE("hostile: factions", "[hostile][data]")
{
    const std::string list = R"([{"id":"archonate","name":"The archonate","role":"incumbent"},{"id":"archaists","name":"The archaists","role":"challenger"},{"id":"mnemones","name":"The mnemones","role":"custodian"}])";
    hostileParser("factions", PROBLEMS_PARSER(parseFactions, Faction), {{"inline", list}});
}
TEST_CASE("hostile: challenger mapping", "[hostile][data]") { hostileParser("challengers", PROBLEMS_PARSER(parseChallengerMapping, ChallengerEntry)); }
TEST_CASE("hostile: custodian mapping", "[hostile][data]") { hostileParser("custodians", PROBLEMS_PARSER(parseCustodianMapping, CustodianEntry)); }
TEST_CASE("hostile: herald mapping", "[hostile][data]") { hostileParser("heralds", PROBLEMS_PARSER(parseHeraldMapping, HeraldEntry)); }
TEST_CASE("hostile: parasite mapping", "[hostile][data]") { hostileParser("parasites", PROBLEMS_PARSER(parseParasiteMapping, ParasiteEntry)); }
TEST_CASE("hostile: refusenik mapping", "[hostile][data]") { hostileParser("refuseniks", PROBLEMS_PARSER(parseRefusenikMapping, RefusenikEntry)); }
TEST_CASE("hostile: capabilities", "[hostile][data]") { hostileParser("capabilities", PROBLEMS_PARSER(parseCapabilities, Capability)); }
TEST_CASE("hostile: power growth", "[hostile][data]") { hostileParser("power growth", PROBLEMS_PARSER(parsePowerGrowth, SpineStat)); }
TEST_CASE("hostile: respec services", "[hostile][data]") { hostileParser("respec", PROBLEMS_PARSER(parseRespecServices, RespecService)); }
TEST_CASE("hostile: enemy reskin layers", "[hostile][data]") { hostileParser("reskin", PROBLEMS_PARSER(parseReskinLayers, ReskinLayer)); }

TEST_CASE("hostile: faction palette", "[hostile][data]")
{
    hostileParser("faction palette", [](std::string_view text) {
        FactionPalette out;
        std::vector<std::string> problems;
        return parseFactionPalette("hostile.json", text, out, problems);
    });
}

TEST_CASE("hostile: trainers", "[hostile][data]")
{
    hostileParser("trainers", [](std::string_view text) {
        std::vector<Trainer> out;
        std::vector<std::string> problems;
        return parseTrainers("hostile.json", text, capabilities(), out, problems);
    });
}

TEST_CASE("hostile: standing tiers", "[hostile][data]")
{
    hostileParser("standing tiers", PROBLEMS_PARSER(parseStandingTiers, StandingTier), {{"inline", kTiers}});
}

TEST_CASE("hostile: standing gates", "[hostile][data]")
{
    hostileParser("standing gates", [](std::string_view text) {
        std::vector<StandingGate> out;
        std::vector<std::string> problems;
        return parseStandingGates("hostile.json", text, tiers(), factions(), out, problems);
    }, {{"inline", kGates}});
}

TEST_CASE("hostile: mission rules", "[hostile][data]")
{
    hostileParser("mission rules", [](std::string_view text) { return graph::mission::parseRuleset(text).error.empty(); });
}

TEST_CASE("hostile: quest expansion rules", "[hostile][data]")
{
    hostileParser("quest rules", [](std::string_view text) { return graph::quest::parseRuleset(text).error.empty(); });
}

TEST_CASE("hostile: hud layout", "[hostile][data]")
{
    hostileParser("hud layout", [](std::string_view text) { return parseHudLayout(text).ok(); });
}

TEST_CASE("hostile: law hud config", "[hostile][data]")
{
    hostileParser("law hud", [](std::string_view text) { return parseLawHudConfig(text).ok(); });
}

TEST_CASE("hostile: autosave policy", "[hostile][data]")
{
    hostileParser("autosave", [](std::string_view text) { return AutosavePolicy::parse(text).has_value(); });
}

TEST_CASE("hostile: material limits", "[hostile][data]")
{
    hostileParser("material limits", [](std::string_view text) {
        MaterialLimits out;
        std::vector<std::string> problems;
        return parseMaterialLimits(text, out, problems);
    });
}

TEST_CASE("hostile: unreal export schema", "[hostile][data]")
{
    hostileParser("ue schema", [](std::string_view text) {
        UeSchema out;
        std::string error;
        return parseUeSchema(text, out, error);
    }, {{"inline", kUeSchema}});
}

TEST_CASE("hostile: dialogue conditions", "[hostile][data]")
{
    const std::vector<std::string> samples = {"trait=bitter", "!debt", "schedule!=sleep"};
    for (const std::string& sample : samples) {
        hostile::TextOptions options;
        options.randomMutations = 400;
        const hostile::TextReport report = hostile::mutateText(sample, [](std::string_view text) {
            DialogueCondition out;
            std::string error;
            return parseCondition(std::string(text), out, error);
        }, options);
        INFO(sample << ": " << hostile::summary(report));
        CHECK(report.ok());
    }
}
