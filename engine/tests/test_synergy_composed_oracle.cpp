#include "ichor/data/axiom.h"
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

struct Shipped {
    std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    DataSet data;
    AxiomSet axioms;
    std::vector<Synergy> defs;
    std::vector<std::string> problems;

    Shipped()
    {
        REQUIRE(data.loadDirectory(root));
        REQUIRE(axioms.loadDirectory(root));
        const bool ok = loadSynergyContentDirectory(root / "synergies", axiomPool(axioms), defs, problems);
        for (const std::string& problem : problems) {
            UNSCOPED_INFO(problem);
        }
        REQUIRE(ok);
    }

    const Synergy* composed() const
    {
        const auto it = std::find_if(defs.begin(), defs.end(), [](const Synergy& s) { return s.id == "composed_oracle"; });
        return it == defs.end() ? nullptr : &*it;
    }
};

}

TEST_CASE("the composed oracle loads as a tier 1 record through the real loader", "[synergy_composed_oracle]")
{
    const Shipped shipped;
    const Synergy* synergy = shipped.composed();
    REQUIRE(synergy != nullptr);
    CHECK(synergy->contentTier == ContentTier::Flagship);
    CHECK(synergy->faction == "mnemones");
    CHECK_FALSE(synergy->dungeonType.empty());
    CHECK_FALSE(synergy->mechanic.empty());
    CHECK_FALSE(synergy->worldName.empty());
}

TEST_CASE("the composed oracle names a faction that exists and leaves no dangling reference", "[synergy_composed_oracle]")
{
    const Shipped shipped;
    const XrefReport report = checkReferences(shipped.data, shipped.axioms, shipped.root);
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
}

TEST_CASE("the composed oracle fires for its axiom pair and for no half of it", "[synergy_composed_oracle]")
{
    const Shipped shipped;
    const auto firedIds = [&](const std::vector<std::string>& axioms) { return detectSynergies(axioms, shipped.defs); };
    const auto has = [](const std::vector<std::string>& ids) { return std::find(ids.begin(), ids.end(), "composed_oracle") != ids.end(); };

    CHECK(has(firedIds({"cataclysm.silence_of_the_oracles", "powers.amphictyony"})));
    CHECK_FALSE(has(firedIds({"cataclysm.silence_of_the_oracles", "powers.league"})));
    CHECK_FALSE(has(firedIds({"cataclysm.flood", "powers.amphictyony"})));
    CHECK_FALSE(has(firedIds({"cataclysm.silence_of_the_oracles"})));

    const std::vector<std::string> fired = detectSynergies({"cataclysm.silence_of_the_oracles", "powers.amphictyony"}, shipped.defs);
    const SynergyOverride result = applyOverrides(fired, shipped.defs, {});
    CHECK(result.factions == std::vector<std::string>{"mnemones"});
    CHECK(result.worldName == "The Composed Oracle");
}
