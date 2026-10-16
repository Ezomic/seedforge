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

TEST_CASE("Deukalion's Flood + Orichalcum loads, resolves its faction and fires for its axiom pair", "[synergy_content]")
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    DataSet data;
    AxiomSet axioms;
    REQUIRE(data.loadDirectory(root));
    REQUIRE(axioms.loadDirectory(root));
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    const bool ok = loadSynergyContentDirectory(root / "synergies", axiomPool(axioms), defs, problems);
    for (const std::string& problem : problems) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(ok);

    const auto it = std::find_if(defs.begin(), defs.end(), [](const Synergy& s) { return s.id == "deukalion_orichalcum"; });
    REQUIRE(it != defs.end());
    CHECK(it->contentTier == ContentTier::Flagship);
    CHECK(it->faction == "metallarchs");
    CHECK(it->dungeonType == "pressurised_wreck");
    CHECK(it->mechanic == "dive_progression");

    const XrefReport report = checkReferences(data, axioms, root);
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());

    const auto fired = detectSynergies({"cataclysm.flood", "law.orichalcum"}, defs);
    REQUIRE_FALSE(fired.empty());
    const SynergyOverride result = applyOverrides(fired, defs, {});
    CHECK(result.factions == std::vector<std::string>{"metallarchs"});
    CHECK(result.dungeonTypes == std::vector<std::string>{"pressurised_wreck"});
    CHECK(result.worldName == "The Drowned Lode");

    CHECK(detectSynergies({"cataclysm.flood", "law.oath"}, defs).empty());
}
