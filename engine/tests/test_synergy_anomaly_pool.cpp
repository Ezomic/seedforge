#include "ichor/data/data_set.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <variant>
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

std::vector<Synergy> anomalies(const std::vector<Synergy>& defs)
{
    std::vector<Synergy> out;
    std::copy_if(defs.begin(), defs.end(), std::back_inserter(out), [](const Synergy& s) { return s.tier == SynergyTier::Anomaly; });
    return out;
}

const std::vector<std::vector<std::string>> kPairs = {
    {"anomaly.tiresias", "cataclysm.silence_of_the_oracles"},
    {"anomaly.underworld", "pressure.unburied"},
};

double poiWeight(const Axiom& axiom, const std::string& key)
{
    const auto& weights = std::get<std::map<std::string, double, std::less<>>>(axiom.blocks.at("poi").at("weights"));
    const auto it = weights.find(key);
    return it == weights.end() ? 0.0 : it->second;
}

// Total poi weight an anomaly axiom contributes to a world, 0 when its synergy does not fire.
// Removing the pool removes the fired synergy and with it the anomaly's reach into the world name.
std::size_t anomalyWorldNames(const std::vector<std::string>& world, const std::vector<Synergy>& defs)
{
    const SynergyOverride result = applyOverrides(detectSynergies(world, defs), defs, {});
    return result.worldName.empty() ? 0 : 1;
}

}

TEST_CASE("the anomaly pool ships anomaly-tier records that validate through the real loader", "[synergy_anomaly_pool]")
{
    Loaded l;
    load(l);
    const std::vector<Synergy> pool = anomalies(l.defs);
    CHECK(pool.size() >= 2);
    std::set<std::string> ids;
    std::set<std::vector<std::string>> axiomSets;
    for (const Synergy& s : pool) {
        CHECK(s.contentTier == ContentTier::TextOnly);
        CHECK_FALSE(s.worldName.empty());
        CHECK(s.worldName.size() <= 64);
        REQUIRE(s.flavour.count("world_description") == 1);
        CHECK(s.flavour.at("world_description").size() <= 400);
        CHECK(ids.insert(s.id).second);
        CHECK(axiomSets.insert(s.axioms).second);
        REQUIRE(s.axioms.size() == 2);
        const auto slotOf = [&](const std::string& id) { return axiomPool(l.axioms).at(id); };
        const bool hasAnomaly = std::any_of(s.axioms.begin(), s.axioms.end(), [&](const std::string& id) { return slotOf(id) == AxiomSlot::Anomaly; });
        CHECK(hasAnomaly);
    }
}

TEST_CASE("every anomaly pool axiom exists and no pair duplicates a shipped synergy of another tier", "[synergy_anomaly_pool]")
{
    Loaded l;
    load(l);
    const AxiomPool pool = axiomPool(l.axioms);
    for (const std::vector<std::string>& pair : kPairs) {
        for (const std::string& id : pair) {
            CHECK(pool.count(id) == 1);
        }
        const auto fired = detectSynergies(pair, l.defs);
        REQUIRE(fired.size() == 1);
        const auto it = std::find_if(l.defs.begin(), l.defs.end(), [&](const Synergy& s) { return s.id == fired[0]; });
        REQUIRE(it != l.defs.end());
        CHECK(it->tier == SynergyTier::Anomaly);
    }
    const XrefReport report = checkReferences(l.data, l.axioms, std::filesystem::path(ICHOR_GAME_DATA_DIR));
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
}

TEST_CASE("each anomaly pair fires for its pair and for nothing less, and renames the world", "[synergy_anomaly_pool]")
{
    Loaded l;
    load(l);
    std::vector<Synergy> removed;
    std::copy_if(l.defs.begin(), l.defs.end(), std::back_inserter(removed), [](const Synergy& s) { return s.tier != SynergyTier::Anomaly; });

    for (const std::vector<std::string>& pair : kPairs) {
        CHECK(anomalyWorldNames(pair, l.defs) == 1);
        CHECK(anomalyWorldNames(pair, removed) == 0);
        for (const std::string& one : pair) {
            CHECK(anomalyWorldNames({one}, l.defs) == 0);
        }
    }
    CHECK(anomalyWorldNames({"anomaly.tiresias", "pressure.unburied"}, l.defs) == 0);
    CHECK(anomalyWorldNames({"anomaly.underworld", "cataclysm.silence_of_the_oracles"}, l.defs) == 0);
}

TEST_CASE("anomaly pool effects are measurable and bounded", "[synergy_anomaly_pool]")
{
    Loaded l;
    load(l);
    for (const std::vector<std::string>& pair : kPairs) {
        const Axiom* anomaly = l.axioms.find(pair[0]);
        REQUIRE(anomaly != nullptr);
        double total = 0.0;
        for (const auto& [key, weight] : std::get<std::map<std::string, double, std::less<>>>(anomaly->blocks.at("poi").at("weights"))) {
            REQUIRE(std::isfinite(weight));
            CHECK(weight > 0.0);
            CHECK(weight <= 10.0);
            total += poiWeight(*anomaly, key);
        }
        CHECK(total > 0.0);
        CHECK(total <= 20.0);

        const SynergyOverride result = applyOverrides(detectSynergies(pair, l.defs), l.defs, {});
        CHECK(result.factions.empty());
        CHECK(result.dungeonTypes.empty());
        CHECK_FALSE(result.worldName.empty());
        CHECK(result.flavour.count("world_description") == 1);
    }
}

TEST_CASE("a world with an anomaly pair plus other synergies never runs more than the active cap", "[synergy_anomaly_pool]")
{
    Loaded l;
    load(l);
    const std::vector<std::string> crowded = {"anomaly.underworld", "pressure.unburied", "cataclysm.artemis_withdrew", "anomaly.tiresias", "cataclysm.silence_of_the_oracles", "law.oath", "powers.amphictyony"};
    const auto fired = detectSynergies(crowded, l.defs);
    CHECK(fired.size() <= kMaxActiveSynergies);
    CHECK(detectSynergies({}, l.defs).empty());
    CHECK(detectSynergies({"anomaly.tiresias", "anomaly.tiresias", "cataclysm.silence_of_the_oracles"}, l.defs) == detectSynergies(kPairs[0], l.defs));
}
