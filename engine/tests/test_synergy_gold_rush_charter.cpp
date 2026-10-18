#include "ichor/data/data_set.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

namespace {

struct Loaded {
    DataSet data;
    AxiomSet axioms;
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
};

void load(Loaded& l)
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    REQUIRE(l.data.loadDirectory(root));
    REQUIRE(l.axioms.loadDirectory(root));
    const bool ok = loadSynergyContentDirectory(root / "synergies", axiomPool(l.axioms), l.defs, l.problems);
    for (const std::string& problem : l.problems) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(ok);
}

const std::vector<std::string> kWorld = {"cataclysm.flood", "law.orichalcum", "powers.league"};

}

TEST_CASE("the Flood + Orichalcum + League triple loads through the real loader", "[synergy_gold_rush_charter]")
{
    Loaded l;
    load(l);
    const auto it = std::find_if(l.defs.begin(), l.defs.end(), [](const Synergy& s) { return s.id == "flood_orichalcum_league"; });
    REQUIRE(it != l.defs.end());
    CHECK(it->tier == SynergyTier::Triple);
    CHECK(it->contentTier == ContentTier::Flagship);
    CHECK(it->faction == "metallarchs");
    CHECK(it->dungeonType == "company_shaft");
    CHECK(it->mechanic == "company_town_charter");
    CHECK(it->worldName == "The Charter Shore");
    CHECK(it->axioms == std::vector<std::string>{"cataclysm.flood", "law.orichalcum", "powers.league"});
}

TEST_CASE("the Flood + Orichalcum + League triple has no dangling reference", "[synergy_gold_rush_charter]")
{
    Loaded l;
    load(l);
    const XrefReport report = checkReferences(l.data, l.axioms, std::filesystem::path(ICHOR_GAME_DATA_DIR));
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
}

TEST_CASE("the triple fires for its three axioms and for no two of them", "[synergy_gold_rush_charter]")
{
    Loaded l;
    load(l);
    const auto fired = detectSynergies(kWorld, l.defs);
    CHECK(std::find(fired.begin(), fired.end(), "flood_orichalcum_league") != fired.end());

    for (const std::vector<std::string>& partial : {std::vector<std::string>{"cataclysm.flood", "law.orichalcum"}, std::vector<std::string>{"cataclysm.flood", "powers.league"}, std::vector<std::string>{"law.orichalcum", "powers.league"}, std::vector<std::string>{"cataclysm.flood", "law.orichalcum", "powers.satrapy"}, std::vector<std::string>{"cataclysm.kronos", "law.orichalcum", "powers.league"}}) {
        const auto none = detectSynergies(partial, l.defs);
        CHECK(std::find(none.begin(), none.end(), "flood_orichalcum_league") == none.end());
    }
}

TEST_CASE("the triple outranks its pairs, takes the world name and stays inside the synergy cap", "[synergy_gold_rush_charter]")
{
    Loaded l;
    load(l);
    const auto fired = detectSynergies(kWorld, l.defs);
    REQUIRE(fired.size() == kMaxActiveSynergies);
    CHECK(fired.front() == "flood_orichalcum_league");

    const SynergyOverride result = applyOverrides(fired, l.defs, {});
    CHECK(result.worldName == "The Charter Shore");
    REQUIRE_FALSE(result.dungeonTypes.empty());
    CHECK(result.dungeonTypes.front() == "company_shaft");
}

TEST_CASE("removing the triple changes the world name and the dungeon types", "[synergy_gold_rush_charter]")
{
    Loaded l;
    load(l);
    std::vector<Synergy> removed;
    std::copy_if(l.defs.begin(), l.defs.end(), std::back_inserter(removed), [](const Synergy& s) { return s.id != "flood_orichalcum_league"; });
    const SynergyOverride without = applyOverrides(detectSynergies(kWorld, removed), removed, {});
    CHECK(without.worldName != "The Charter Shore");
    const auto& types = without.dungeonTypes;
    CHECK(std::find(types.begin(), types.end(), "company_shaft") == types.end());
}

TEST_CASE("the triple tolerates hostile axiom lists", "[synergy_gold_rush_charter]")
{
    Loaded l;
    load(l);
    CHECK(detectSynergies({}, l.defs).empty());
    const auto repeated = detectSynergies({"cataclysm.flood", "cataclysm.flood", "law.orichalcum", "powers.league", "powers.league"}, l.defs);
    CHECK(std::find(repeated.begin(), repeated.end(), "flood_orichalcum_league") != repeated.end());
    CHECK(detectSynergies({"", "no.such", "cataclysm.flood"}, l.defs).empty());
}
