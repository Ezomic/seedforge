#include "ichor/data/data_set.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace ichor;

namespace {

struct Loaded {
    DataSet data;
    AxiomSet axioms;
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    bool ok = false;
};

void load(Loaded& l)
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    REQUIRE(l.data.loadDirectory(root));
    REQUIRE(l.axioms.loadDirectory(root));
    l.ok = loadSynergyContentDirectory(root / "synergies", axiomPool(l.axioms), l.defs, l.problems);
    for (const std::string& problem : l.problems) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(l.ok);
}

const Synergy* find(const std::vector<Synergy>& defs)
{
    const auto it = std::find_if(defs.begin(), defs.end(), [](const Synergy& s) { return s.id == "silence_oath"; });
    return it == defs.end() ? nullptr : &*it;
}

}

TEST_CASE("the Silence + Oath flagship loads through the real loader as tier 1", "[synergy_oathkeepers]")
{
    Loaded l;
    load(l);
    const Synergy* s = find(l.defs);
    REQUIRE(s != nullptr);
    CHECK(s->contentTier == ContentTier::Flagship);
    CHECK(s->faction == "horkos_brokers");
    CHECK_FALSE(s->dungeonType.empty());
    CHECK(s->mechanic == "traceable_oath");
    CHECK_FALSE(s->worldName.empty());
}

TEST_CASE("the Silence + Oath flagship has no dangling reference", "[synergy_oathkeepers]")
{
    Loaded l;
    load(l);
    const XrefReport report = checkReferences(l.data, l.axioms, std::filesystem::path(ICHOR_GAME_DATA_DIR));
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
}

TEST_CASE("the Silence + Oath flagship fires for its axiom pair and for nothing less", "[synergy_oathkeepers]")
{
    Loaded l;
    load(l);
    const auto fired = detectSynergies({"cataclysm.silence_of_the_oracles", "law.oath"}, l.defs);
    const SynergyOverride result = applyOverrides(fired, l.defs, {});
    CHECK(std::find(result.factions.begin(), result.factions.end(), "horkos_brokers") != result.factions.end());
    CHECK(std::find(result.dungeonTypes.begin(), result.dungeonTypes.end(), "tally_vault") != result.dungeonTypes.end());
    CHECK(result.worldName == "The Unreleased Lands");

    for (const std::vector<std::string>& partial : {std::vector<std::string>{"cataclysm.silence_of_the_oracles"}, std::vector<std::string>{"law.oath"}, std::vector<std::string>{"cataclysm.silence_of_the_oracles", "law.hecatomb"}}) {
        const SynergyOverride none = applyOverrides(detectSynergies(partial, l.defs), l.defs, {});
        CHECK(std::find(none.factions.begin(), none.factions.end(), "horkos_brokers") == none.factions.end());
    }
}
