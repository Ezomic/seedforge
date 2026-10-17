#include "ichor/data/axiom.h"
#include "ichor/data/data_set.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

using namespace ichor;

namespace {

const Synergy& shipped(const std::vector<Synergy>& defs)
{
    const auto it = std::find_if(defs.begin(), defs.end(), [](const Synergy& s) { return s.id == "demeters_grief_mysteries"; });
    REQUIRE(it != defs.end());
    return *it;
}

}

TEST_CASE("Demeter's Grief and The Mysteries are authored and Flood still conflicts with Grief", "[synergy_content]")
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(root));
    const Axiom* grief = axioms.find("cataclysm.demeters_grief");
    const Axiom* mysteries = axioms.find("law.mysteries");
    const Axiom* flood = axioms.find("cataclysm.flood");
    REQUIRE(grief != nullptr);
    REQUIRE(mysteries != nullptr);
    REQUIRE(flood != nullptr);
    CHECK(grief->slot == AxiomSlot::Cataclysm);
    CHECK(mysteries->slot == AxiomSlot::Law);
    CHECK(grief->blocks.size() >= 4);
    CHECK(mysteries->blocks.size() >= 4);
    CHECK(std::find(flood->conflicts.begin(), flood->conflicts.end(), "cataclysm.demeters_grief") != flood->conflicts.end());
}

TEST_CASE("the Hierophants synergy loads, passes xref and fires for its axiom pair", "[synergy_content]")
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    DataSet data;
    AxiomSet axioms;
    REQUIRE(data.loadDirectory(root));
    REQUIRE(axioms.loadDirectory(root));
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    const bool loaded = loadSynergyContentDirectory(root / "synergies", axiomPool(axioms), defs, problems);
    for (const std::string& problem : problems) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(loaded);

    const Synergy& synergy = shipped(defs);
    CHECK(synergy.contentTier == ContentTier::Flagship);

    const XrefReport report = checkReferences(data, axioms, root);
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());

    const auto fired = detectSynergies({"cataclysm.demeters_grief", "law.mysteries"}, defs);
    REQUIRE(std::find(fired.begin(), fired.end(), "demeters_grief_mysteries") != fired.end());
    const SynergyOverride result = applyOverrides(fired, defs, {});
    CHECK(result.factions == std::vector<std::string>{"hierophants"});
    CHECK(result.dungeonTypes == std::vector<std::string>{"sealed_telesterion"});
    CHECK(result.worldName == "The Unreturned");

    CHECK(detectSynergies({"cataclysm.demeters_grief", "law.hecatomb"}, defs).empty());
    CHECK(detectSynergies({"cataclysm.flood", "law.mysteries"}, defs).empty());
}

TEST_CASE("the Hierophants are a real faction of the Mysteries, distinct from the Telestai stand-in", "[synergy_content]")
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    DataSet data;
    REQUIRE(data.loadDirectory(root));

    const DataRecord* hierophants = data.record("factions", "hierophants");
    REQUIRE(hierophants != nullptr);
    CHECK(data.field("factions", "hierophants", "name") != nullptr);
    const DataValue* axiom = data.field("factions", "hierophants", "axiom");
    REQUIRE(axiom != nullptr);
    REQUIRE(std::holds_alternative<std::string>(*axiom));
    CHECK(std::get<std::string>(*axiom) == "law.mysteries");
    CHECK(data.record("factions", "telestai") != nullptr);
}

TEST_CASE("Demeter's Grief and The Mysteries keep every POI weight inside the range of the sibling axioms", "[synergy_content]")
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(root));
    for (const char* id : {"cataclysm.demeters_grief", "law.mysteries"}) {
        const Axiom* axiom = axioms.find(id);
        REQUIRE(axiom != nullptr);
        const auto poi = axiom->blocks.find("poi");
        REQUIRE(poi != axiom->blocks.end());
        const auto weights = poi->second.find("weights");
        REQUIRE(weights != poi->second.end());
        const auto* map = std::get_if<std::map<std::string, double, std::less<>>>(&weights->second);
        REQUIRE(map != nullptr);
        for (const auto& [poiId, weight] : *map) {
            INFO(id << " " << poiId);
            CHECK(weight > 0.0);
            CHECK(weight <= 3.0);
        }
    }
}
