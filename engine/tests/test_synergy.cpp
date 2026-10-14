#include "ichor/data/synergy.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
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
