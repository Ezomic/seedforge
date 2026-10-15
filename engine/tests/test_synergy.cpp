#include "ichor/data/synergy.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace ichor;

namespace {

AxiomPool pool()
{
    return {
        {"cataclysm.artemis_withdrew", AxiomSlot::Cataclysm},
        {"cataclysm.silence_of_oracles", AxiomSlot::Cataclysm},
        {"cataclysm.flood", AxiomSlot::Cataclysm},
        {"cataclysm.unbinding", AxiomSlot::Cataclysm},
        {"law.hecatomb", AxiomSlot::Law},
        {"law.oath", AxiomSlot::Law},
        {"law.mysteries", AxiomSlot::Law},
        {"powers.satrapy", AxiomSlot::Powers},
        {"powers.league", AxiomSlot::Powers},
        {"pressure.return", AxiomSlot::Pressure},
        {"pressure.opening", AxiomSlot::Pressure},
        {"pressure.unburied", AxiomSlot::Pressure},
        {"anomaly.tiresias", AxiomSlot::Anomaly},
        {"anomaly.labyrinth", AxiomSlot::Anomaly},
    };
}

std::string entry(const std::string& id, const std::string& tier, const std::string& axioms)
{
    return "{\"id\":\"" + id + "\",\"tier\":\"" + tier + "\",\"axioms\":[" + axioms + "]}";
}

std::string file(const std::string& entries) { return "{\"synergies\":[" + entries + "]}"; }

std::vector<std::string> problemsOf(const std::string& text, std::vector<Synergy>* out = nullptr)
{
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    const bool ok = parseSynergies("synergies/t.json", text, pool(), defs, problems);
    REQUIRE(ok == problems.empty());
    if (out != nullptr) {
        *out = defs;
    }
    return problems;
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

Synergy make(const std::string& id, SynergyTier tier, std::vector<std::string> axioms)
{
    std::sort(axioms.begin(), axioms.end());
    return Synergy{id, tier, std::move(axioms)};
}

using Ids = std::vector<std::string>;

}

TEST_CASE("the example file from doc 02 validates and loads sorted by id")
{
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    REQUIRE(loadSynergyFile(std::filesystem::path(ICHOR_TEST_DATA) / "synergies" / "example.json", pool(), defs, problems));
    REQUIRE(problems.empty());
    REQUIRE(defs.size() == 3);
    REQUIRE(defs[0].id == "hekatombaia");
    REQUIRE(defs[0].tier == SynergyTier::CataclysmLaw);
    REQUIRE(defs[0].axioms == Ids{"cataclysm.artemis_withdrew", "law.hecatomb"});
    REQUIRE(defs[1].id == "oathless_return");
    REQUIRE(defs[1].tier == SynergyTier::Triple);
    REQUIRE(defs[2].tier == SynergyTier::Pressure);
}

TEST_CASE("a missing file is a problem, not a crash")
{
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    REQUIRE_FALSE(loadSynergyFile(std::filesystem::path(ICHOR_TEST_DATA) / "synergies" / "nope.json", pool(), defs, problems));
    REQUIRE(mentions(problems, "cannot read"));
}

TEST_CASE("validation reports every problem with its path")
{
    REQUIRE(mentions(problemsOf(file(entry("a", "cataclysm_law", "\"cataclysm.flood\",\"law.nope\""))), "synergies[0].axioms[1]: unknown axiom"));
    REQUIRE(mentions(problemsOf(file(entry("a", "cataclysm_law", "\"cataclysm.flood\",\"cataclysm.flood\""))), "synergies[0].axioms[1]: duplicate"));
    REQUIRE(mentions(problemsOf(file(entry("a", "cataclysm_law", "\"cataclysm.flood\",\"law.oath\"") + "," + entry("a", "pressure", "\"law.oath\",\"pressure.return\""))), "synergies[1].id: duplicate"));
    REQUIRE(mentions(problemsOf(file(entry("a", "cataclysm_law", "\"cataclysm.flood\""))), "synergies[0].axioms: expected 2 axioms"));
    REQUIRE(mentions(problemsOf(file(entry("a", "triple", "\"cataclysm.flood\",\"law.oath\""))), "synergies[0].axioms: expected 3 axioms"));
    REQUIRE(mentions(problemsOf(file(entry("a", "mythic", "\"cataclysm.flood\",\"law.oath\""))), "synergies[0].tier: expected one of"));
    REQUIRE(mentions(problemsOf(file(entry("Bad Id", "cataclysm_law", "\"cataclysm.flood\",\"law.oath\""))), "synergies[0].id"));
    REQUIRE(mentions(problemsOf(file("{\"id\":\"a\",\"tier\":\"pressure\"}")), "synergies[0].axioms: missing"));
    REQUIRE(mentions(problemsOf(file(entry("a", "cataclysm_law", "7,\"law.oath\""))), "synergies[0].axioms[0]: expected a string"));
}

TEST_CASE("every problem in a file is reported, not just the first")
{
    const auto problems = problemsOf(file(entry("a", "pressure", "\"law.nope\",\"pressure.return\"") + "," + entry("b", "mythic", "\"law.oath\",\"law.oath\"")));
    REQUIRE(mentions(problems, "synergies[0].axioms[0]: unknown axiom"));
    REQUIRE(mentions(problems, "synergies[1].tier"));
    REQUIRE(mentions(problems, "synergies[1].axioms[1]: duplicate"));
}

TEST_CASE("the tier has to match the slots of the axioms")
{
    REQUIRE(problemsOf(file(entry("a", "cataclysm_law", "\"cataclysm.flood\",\"law.oath\""))).empty());
    REQUIRE(problemsOf(file(entry("a", "cataclysm_powers", "\"powers.league\",\"cataclysm.flood\""))).empty());
    REQUIRE(problemsOf(file(entry("a", "law_powers", "\"law.oath\",\"powers.league\""))).empty());
    REQUIRE(problemsOf(file(entry("a", "pressure", "\"law.oath\",\"pressure.return\""))).empty());
    REQUIRE(problemsOf(file(entry("a", "anomaly", "\"pressure.unburied\",\"anomaly.tiresias\""))).empty());
    REQUIRE(mentions(problemsOf(file(entry("a", "cataclysm_law", "\"cataclysm.flood\",\"powers.league\""))), "synergies[0].tier: cataclysm_law needs"));
    REQUIRE(mentions(problemsOf(file(entry("a", "pressure", "\"law.oath\",\"anomaly.tiresias\""))), "synergies[0].tier: pressure needs"));
    REQUIRE(mentions(problemsOf(file(entry("a", "anomaly", "\"anomaly.labyrinth\",\"anomaly.tiresias\""))), "synergies[0].tier: anomaly needs"));
    REQUIRE(mentions(problemsOf(file(entry("a", "triple", "\"cataclysm.flood\",\"cataclysm.unbinding\",\"law.oath\""))), "synergies[0].tier: triple needs"));
}

TEST_CASE("more slot rules, and nothing is output on failure")
{
    REQUIRE(mentions(problemsOf(file(entry("a", "pressure", "\"pressure.return\",\"pressure.opening\""))), "synergies[0].tier: pressure needs"));
    REQUIRE(problemsOf(file(entry("a", "anomaly", "\"anomaly.tiresias\",\"law.oath\""))).empty());
    REQUIRE(mentions(problemsOf(file(entry("a", "triple", "\"cataclysm.flood\",\"cataclysm.unbinding\",\"law.oath\""))), "triple needs"));
    REQUIRE(mentions(problemsOf(file(entry("a", "mythic", "\"law.oath\",\"law.mysteries\",\"powers.league\",\"powers.satrapy\""))), "expected 2 or 3 axioms"));
    REQUIRE(mentions(problemsOf("{\"synergies\":[[[[[[[[[[1]]]]]]]]]]}"), "nested deeper"));
    std::vector<Synergy> defs = {make("keep", SynergyTier::Pressure, {"law.oath", "pressure.return"})};
    std::vector<std::string> problems;
    REQUIRE_FALSE(parseSynergies("t", file(entry("Bad", "pressure", "\"law.oath\",\"pressure.return\"")), pool(), defs, problems));
    REQUIRE(defs.size() == 1);
    REQUIRE(defs[0].id == "keep");
}

TEST_CASE("structural garbage is refused without throwing")
{
    REQUIRE(mentions(problemsOf(""), "empty file"));
    REQUIRE(mentions(problemsOf("not json"), "not valid JSON"));
    REQUIRE(mentions(problemsOf("[]"), "expected an object"));
    REQUIRE(mentions(problemsOf("{}"), "synergies: missing"));
    REQUIRE(mentions(problemsOf("{\"synergies\":{}}"), "synergies: expected an array"));
    REQUIRE(mentions(problemsOf("{\"synergies\":[3]}"), "synergies[0]: expected an object"));
    REQUIRE(mentions(problemsOf("{\"synergies\":[],\"extra\":1}"), "extra: unknown key"));
    REQUIRE(mentions(problemsOf(file("{\"zzz\":1,\"id\":\"a\",\"tier\":\"pressure\",\"axioms\":[\"law.oath\",\"pressure.return\"]}")), "synergies[0].zzz: unknown key"));
    REQUIRE(problemsOf(file("")).empty());
}

TEST_CASE("a huge definition file is refused by size and by count")
{
    std::string many;
    for (int i = 0; i < 5000; ++i) {
        many += (i == 0 ? "" : ",") + entry("s" + std::to_string(i), "pressure", "\"law.oath\",\"pressure.return\"");
    }
    REQUIRE(mentions(problemsOf(file(many)), "more than"));
    REQUIRE(mentions(problemsOf(std::string(5 * 1024 * 1024, ' ')), "larger than"));
}

TEST_CASE("an empty set, or one smaller than any synergy, fires nothing")
{
    const std::vector<Synergy> defs = {make("a", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.oath"})};
    REQUIRE(detectSynergies({}, defs).empty());
    REQUIRE(detectSynergies({"cataclysm.flood"}, defs).empty());
    REQUIRE(detectSynergies({"cataclysm.flood", "powers.league"}, defs).empty());
    REQUIRE(detectSynergies({"cataclysm.flood", "law.oath"}, {}).empty());
}

TEST_CASE("a synergy fires when all its axioms are present, whatever the order or repeats")
{
    const std::vector<Synergy> defs = {make("a", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.oath"})};
    REQUIRE(detectSynergies({"cataclysm.flood", "law.oath", "powers.league"}, defs) == Ids{"a"});
    REQUIRE(detectSynergies({"powers.league", "law.oath", "cataclysm.flood"}, defs) == Ids{"a"});
    REQUIRE(detectSynergies({"law.oath", "law.oath", "cataclysm.flood", "cataclysm.flood"}, defs) == Ids{"a"});
    REQUIRE(detectSynergies({"law.oath", "law.oath"}, defs).empty());
}

TEST_CASE("synergies fire in tier priority, then id")
{
    const std::vector<Synergy> defs = {
        make("z_cl", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.oath"}),
        make("a_cl", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.mysteries"}),
        make("cp", SynergyTier::CataclysmPowers, {"cataclysm.flood", "powers.league"}),
        make("lp", SynergyTier::LawPowers, {"law.oath", "powers.league"}),
        make("pr", SynergyTier::Pressure, {"law.oath", "pressure.return"}),
        make("an", SynergyTier::Anomaly, {"anomaly.tiresias", "cataclysm.flood"}),
    };
    REQUIRE(detectSynergies({"cataclysm.flood", "law.oath", "powers.league", "pressure.return", "anomaly.tiresias"}, defs) == Ids{"z_cl", "cp"});
    REQUIRE(detectSynergies({"cataclysm.flood", "law.mysteries", "law.oath"}, defs) == Ids{"a_cl", "z_cl"});
    REQUIRE(detectSynergies({"law.oath", "powers.league", "pressure.return", "anomaly.tiresias", "cataclysm.unbinding"}, defs) == Ids{"lp", "pr"});
    REQUIRE(detectSynergies({"cataclysm.flood", "anomaly.tiresias", "powers.satrapy"}, defs) == Ids{"an"});
}

TEST_CASE("at most two fire and the lower tiers are never reached")
{
    const std::vector<Synergy> defs = {
        make("lp", SynergyTier::LawPowers, {"law.oath", "powers.league"}),
        make("pr", SynergyTier::Pressure, {"law.oath", "pressure.return"}),
        make("an", SynergyTier::Anomaly, {"anomaly.tiresias", "law.oath"}),
    };
    REQUIRE(detectSynergies({"law.oath", "powers.league", "pressure.return", "anomaly.tiresias"}, defs) == Ids{"lp", "pr"});
    REQUIRE(detectSynergies({"law.oath", "pressure.return", "anomaly.tiresias"}, defs) == Ids{"pr", "an"});
    REQUIRE(detectSynergies({"anomaly.tiresias", "pressure.return", "powers.league", "law.oath"}, defs) == Ids{"lp", "pr"});
}

TEST_CASE("a triple resolves first and its pairs still fire if there is room")
{
    const std::vector<Synergy> defs = {
        make("pair", SynergyTier::CataclysmLaw, {"cataclysm.silence_of_oracles", "law.oath"}),
        make("trio", SynergyTier::Triple, {"cataclysm.silence_of_oracles", "law.oath", "pressure.return"}),
        make("pr", SynergyTier::Pressure, {"law.oath", "pressure.return"}),
    };
    const Ids rolled = {"pressure.return", "law.oath", "cataclysm.silence_of_oracles"};
    REQUIRE(detectSynergies(rolled, defs) == Ids{"trio", "pair"});
    REQUIRE(detectSynergies({"law.oath", "cataclysm.silence_of_oracles"}, defs) == Ids{"pair"});
    REQUIRE(detectSynergies({"law.oath", "pressure.return"}, defs) == Ids{"pr"});
}

TEST_CASE("overlapping synergies sharing an axiom both fire")
{
    const std::vector<Synergy> defs = {
        make("x", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.oath"}),
        make("y", SynergyTier::CataclysmPowers, {"cataclysm.flood", "powers.league"}),
    };
    REQUIRE(detectSynergies({"cataclysm.flood", "law.oath", "powers.league"}, defs) == Ids{"x", "y"});
}

TEST_CASE("the result does not depend on the order of the definitions")
{
    std::vector<Synergy> defs = {
        make("b", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.oath"}),
        make("a", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.mysteries"}),
        make("c", SynergyTier::CataclysmPowers, {"cataclysm.flood", "powers.league"}),
    };
    const Ids rolled = {"cataclysm.flood", "law.oath", "law.mysteries", "powers.league"};
    const Ids first = detectSynergies(rolled, defs);
    REQUIRE(first == Ids{"a", "b"});
    std::reverse(defs.begin(), defs.end());
    REQUIRE(detectSynergies(rolled, defs) == first);
}

TEST_CASE("a definition naming an axiom nobody rolled simply does not fire")
{
    const std::vector<Synergy> defs = {make("ghost", SynergyTier::CataclysmLaw, {"cataclysm.removed", "law.oath"})};
    REQUIRE(detectSynergies({"cataclysm.flood", "law.oath"}, defs).empty());
}

TEST_CASE("one match among many definitions is found")
{
    std::vector<Synergy> defs;
    for (int i = 0; i < 20000; ++i) {
        defs.push_back(make("s" + std::to_string(1000000 + i), SynergyTier::Pressure, {"law.oath", "pressure.other"}));
    }
    defs.push_back(make("hit", SynergyTier::Pressure, {"law.oath", "pressure.return"}));
    REQUIRE(detectSynergies({"law.oath", "pressure.return"}, defs) == Ids{"hit"});
}

namespace {

std::string withOverride(const std::string& fields)
{
    return file("{\"id\":\"hekatombaia\",\"tier\":\"cataclysm_law\",\"axioms\":[\"cataclysm.artemis_withdrew\",\"law.hecatomb\"]," + fields + "}");
}

Synergy overriding(const std::string& id, SynergyTier tier, std::vector<std::string> axioms, const std::string& faction, const std::string& dungeon, const std::string& world, std::map<std::string, std::string> flavour)
{
    Synergy synergy = make(id, tier, std::move(axioms));
    synergy.faction = faction;
    synergy.dungeonType = dungeon;
    synergy.worldName = world;
    synergy.flavour = std::move(flavour);
    return synergy;
}

}

TEST_CASE("a synergy without override fields loads with every override empty")
{
    std::vector<Synergy> defs;
    REQUIRE(problemsOf(file(entry("a", "cataclysm_law", "\"cataclysm.flood\",\"law.oath\"")), &defs).empty());
    REQUIRE(defs.size() == 1);
    REQUIRE(defs[0].faction.empty());
    REQUIRE(defs[0].dungeonType.empty());
    REQUIRE(defs[0].worldName.empty());
    REQUIRE(defs[0].flavour.empty());
}

TEST_CASE("override fields parse")
{
    std::vector<Synergy> defs;
    const auto problems = problemsOf(withOverride("\"faction\":\"hekatombaia\",\"dungeon_type\":\"sealed_telesterion\",\"world_name\":\"The Tithed Shore\",\"flavour\":{\"tavern_rumour\":\"The levy is paid in breath.\",\"shrine\":\"No beast stands at the altar.\"}"), &defs);
    REQUIRE(problems.empty());
    REQUIRE(defs[0].faction == "hekatombaia");
    REQUIRE(defs[0].dungeonType == "sealed_telesterion");
    REQUIRE(defs[0].worldName == "The Tithed Shore");
    REQUIRE(defs[0].flavour.size() == 2);
    REQUIRE(defs[0].flavour.at("shrine") == "No beast stands at the altar.");
}

TEST_CASE("bad override fields are reported with their path")
{
    REQUIRE(mentions(problemsOf(withOverride("\"faction\":\"Bad Id\"")), "synergies[0].faction"));
    REQUIRE(mentions(problemsOf(withOverride("\"faction\":7")), "synergies[0].faction: expected a string"));
    REQUIRE(mentions(problemsOf(withOverride("\"dungeon_type\":\"\"")), "synergies[0].dungeon_type"));
    REQUIRE(mentions(problemsOf(withOverride("\"world_name\":\"   \"")), "synergies[0].world_name"));
    REQUIRE(mentions(problemsOf(withOverride("\"world_name\":\"" + std::string(500, 'x') + "\"")), "synergies[0].world_name"));
    REQUIRE(mentions(problemsOf(withOverride("\"flavour\":[]")), "synergies[0].flavour: expected an object"));
    REQUIRE(mentions(problemsOf(withOverride("\"flavour\":{\"Bad Key\":\"x\"}")), "synergies[0].flavour"));
    REQUIRE(mentions(problemsOf(withOverride("\"flavour\":{\"shrine\":3}")), "synergies[0].flavour.shrine: expected a string"));
    REQUIRE(mentions(problemsOf(withOverride("\"flavour\":{\"shrine\":\"\"}")), "synergies[0].flavour.shrine"));
}

TEST_CASE("a synergy overrides default flavour and injects faction, dungeon type and world name")
{
    const std::vector<Synergy> defs = {overriding("hekatombaia", SynergyTier::CataclysmLaw, {"cataclysm.artemis_withdrew", "law.hecatomb"}, "hekatombaia", "sealed_telesterion", "The Tithed Shore", {{"shrine", "No beast stands at the altar."}})};
    const std::map<std::string, std::string> defaults = {{"shrine", "Pilgrims leave oxen."}, {"market", "Grain is cheap."}};
    const SynergyOverride result = applyOverrides({"hekatombaia"}, defs, defaults);
    REQUIRE(result.factions == Ids{"hekatombaia"});
    REQUIRE(result.dungeonTypes == Ids{"sealed_telesterion"});
    REQUIRE(result.worldName == "The Tithed Shore");
    REQUIRE(result.flavour.at("shrine") == "No beast stands at the altar.");
    REQUIRE(result.flavour.at("market") == "Grain is cheap.");
    REQUIRE(result.flavour.size() == 2);
}

TEST_CASE("nothing fired leaves the defaults untouched")
{
    const std::map<std::string, std::string> defaults = {{"shrine", "Pilgrims leave oxen."}};
    const SynergyOverride result = applyOverrides({}, {}, defaults);
    REQUIRE(result.factions.empty());
    REQUIRE(result.dungeonTypes.empty());
    REQUIRE(result.worldName.empty());
    REQUIRE(result.flavour == defaults);
}

TEST_CASE("two fired synergies resolve in firing order: first world name and first flavour win, factions and dungeons accumulate")
{
    const std::vector<Synergy> defs = {
        overriding("second", SynergyTier::LawPowers, {"law.oath", "powers.league"}, "league_wardens", "customs_house", "The Ledger Coast", {{"shrine", "second shrine"}, {"harbour", "second harbour"}}),
        overriding("first", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.oath"}, "oathkeepers", "sunken_court", "The Unreleased", {{"shrine", "first shrine"}}),
    };
    const SynergyOverride result = applyOverrides({"first", "second"}, defs, {});
    REQUIRE(result.factions == Ids{"oathkeepers", "league_wardens"});
    REQUIRE(result.dungeonTypes == Ids{"sunken_court", "customs_house"});
    REQUIRE(result.worldName == "The Unreleased");
    REQUIRE(result.flavour.at("shrine") == "first shrine");
    REQUIRE(result.flavour.at("harbour") == "second harbour");
}

TEST_CASE("a first synergy without a world name leaves it to the second; repeated factions are listed once; unknown ids are ignored")
{
    const std::vector<Synergy> defs = {
        overriding("a", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.oath"}, "oathkeepers", "", "", {}),
        overriding("b", SynergyTier::LawPowers, {"law.oath", "powers.league"}, "oathkeepers", "", "The Ledger Coast", {}),
    };
    const SynergyOverride result = applyOverrides({"a", "ghost", "b"}, defs, {});
    REQUIRE(result.factions == Ids{"oathkeepers"});
    REQUIRE(result.dungeonTypes.empty());
    REQUIRE(result.worldName == "The Ledger Coast");
}

TEST_CASE("applyOverrides ignores the order of the definitions")
{
    std::vector<Synergy> defs = {
        overriding("a", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.oath"}, "f1", "d1", "W1", {{"k", "v1"}}),
        overriding("b", SynergyTier::LawPowers, {"law.oath", "powers.league"}, "f2", "d2", "W2", {{"k", "v2"}}),
    };
    const SynergyOverride forward = applyOverrides({"a", "b"}, defs, {});
    std::reverse(defs.begin(), defs.end());
    const SynergyOverride backward = applyOverrides({"a", "b"}, defs, {});
    REQUIRE(forward.factions == backward.factions);
    REQUIRE(forward.dungeonTypes == backward.dungeonTypes);
    REQUIRE(forward.worldName == backward.worldName);
    REQUIRE(forward.flavour == backward.flavour);

    const SynergyOverride swapped = applyOverrides({"b", "a"}, defs, {});
    REQUIRE(swapped.factions == Ids{"f2", "f1"});
    REQUIRE(swapped.dungeonTypes == Ids{"d2", "d1"});
    REQUIRE(swapped.worldName == "W2");
    REQUIRE(swapped.flavour.at("k") == "v2");
}

TEST_CASE("override size limits sit exactly at their boundaries")
{
    REQUIRE(problemsOf(withOverride("\"world_name\":\"" + std::string(80, 'x') + "\"")).empty());
    REQUIRE(mentions(problemsOf(withOverride("\"world_name\":\"" + std::string(81, 'x') + "\"")), "at most 80 bytes"));
    REQUIRE(problemsOf(withOverride("\"faction\":\"" + std::string(64, 'a') + "\"")).empty());
    REQUIRE(mentions(problemsOf(withOverride("\"faction\":\"" + std::string(65, 'a') + "\"")), "lowercase ascii id"));
    REQUIRE(mentions(problemsOf(withOverride("\"dungeon_type\":\"" + std::string(65, 'a') + "\"")), "lowercase ascii id"));
    REQUIRE(problemsOf(withOverride("\"flavour\":{\"k\":\"" + std::string(1000, 'x') + "\"}")).empty());
    REQUIRE(mentions(problemsOf(withOverride("\"flavour\":{\"k\":\"" + std::string(1001, 'x') + "\"}")), "at most 1000 bytes"));
    REQUIRE(mentions(problemsOf(withOverride("\"flavour\":{\"" + std::string(65, 'k') + "\":\"x\"}")), "slot ids"));

    std::string slots;
    for (int i = 0; i < 65; ++i) {
        slots += std::string(slots.empty() ? "" : ",") + "\"s" + std::to_string(i) + "\":\"x\"";
    }
    REQUIRE(mentions(problemsOf(withOverride("\"flavour\":{" + slots + "}")), "more than 64 entries"));
}

TEST_CASE("control characters are rejected in free text, a newline is allowed in flavour")
{
    REQUIRE(mentions(problemsOf(withOverride("\"world_name\":\"a\\u0000b\"")), "synergies[0].world_name"));
    REQUIRE(mentions(problemsOf(withOverride("\"world_name\":\"a\\nb\"")), "synergies[0].world_name"));
    REQUIRE(mentions(problemsOf(withOverride("\"flavour\":{\"k\":\"a\\u001bb\"}")), "synergies[0].flavour.k"));
    REQUIRE(problemsOf(withOverride("\"flavour\":{\"k\":\"a\\nb\"}")).empty());
}

TEST_CASE("null and empty override values")
{
    REQUIRE(mentions(problemsOf(withOverride("\"faction\":null")), "synergies[0].faction: expected a string, got null"));
    REQUIRE(mentions(problemsOf(withOverride("\"flavour\":{\"k\":null}")), "synergies[0].flavour.k: expected a string, got null"));
    std::vector<Synergy> defs;
    REQUIRE(problemsOf(withOverride("\"flavour\":{}"), &defs).empty());
    REQUIRE(defs[0].flavour.empty());
}

TEST_CASE("a default slot survives when only a later synergy overrides another slot, and repeated fired ids count once")
{
    const std::vector<Synergy> defs = {
        overriding("a", SynergyTier::CataclysmLaw, {"cataclysm.flood", "law.oath"}, "f1", "", "", {}),
        overriding("b", SynergyTier::LawPowers, {"law.oath", "powers.league"}, "", "", "", {{"harbour", "h"}}),
    };
    const SynergyOverride result = applyOverrides({"a", "a", "b"}, defs, {{"shrine", "s"}});
    REQUIRE(result.factions == Ids{"f1"});
    REQUIRE(result.flavour.at("shrine") == "s");
    REQUIRE(result.flavour.at("harbour") == "h");
}
