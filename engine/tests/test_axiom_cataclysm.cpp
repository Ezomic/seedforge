#include "ichor/data/axiom.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>
#include <string_view>

using namespace ichor;

namespace {

struct Expected {
    std::string_view id;
    std::string_view title;
    std::vector<std::string_view> blocks;
};

const std::array<Expected, 3>& cataclysms()
{
    static const std::array<Expected, 3> list = {{
        {"cataclysm.silence_of_the_oracles", "The Silence of the Oracles", {"poi", "economy", "enemies", "palette", "audio"}},
        {"cataclysm.flood", "Deukalion's Flood", {"terrain", "settlement", "biomes", "poi", "enemies", "traversal", "economy", "palette", "audio"}},
        {"cataclysm.artemis_withdrew", "Artemis Withdrew", {"biomes", "poi", "enemies", "economy", "palette", "audio"}},
    }};
    return list;
}

bool has(const std::vector<std::string>& list, std::string_view word)
{
    return std::find(list.begin(), list.end(), word) != list.end();
}

const std::vector<std::string>& words(const Axiom& axiom, const char* block, const char* key)
{
    return std::get<std::vector<std::string>>(axiom.blocks.at(block).at(key));
}

}

TEST_CASE("every axiom file in the repo data validates and ids are unique")
{
    AxiomSet set;
    const bool loaded = set.loadDirectory(ICHOR_REPO_DATA);
    for (const std::string& problem : set.problems()) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(loaded);
    REQUIRE(set.axioms().size() >= cataclysms().size());
}

TEST_CASE("the three Cataclysm axioms are authored with their directive blocks")
{
    AxiomSet set;
    REQUIRE(set.loadDirectory(ICHOR_REPO_DATA));
    for (const Expected& expected : cataclysms()) {
        CAPTURE(expected.id);
        const Axiom* axiom = set.find(expected.id);
        REQUIRE(axiom != nullptr);
        REQUIRE(axiom->slot == AxiomSlot::Cataclysm);
        REQUIRE(axiom->title == expected.title);
        REQUIRE_FALSE(axiom->tags.empty());
        REQUIRE(axiom->blocks.size() >= 4);
        for (const std::string_view block : expected.blocks) {
            CAPTURE(block);
            REQUIRE(axiom->blocks.count(std::string(block)) == 1);
        }
    }
    const auto cataclysmCount = std::count_if(set.axioms().begin(), set.axioms().end(), [](const auto& entry) { return entry.second.slot == AxiomSlot::Cataclysm; });
    REQUIRE(cataclysmCount >= 3);
}

TEST_CASE("conflicts never name the axiom itself and use the slot's own ids")
{
    AxiomSet set;
    REQUIRE(set.loadDirectory(ICHOR_REPO_DATA));
    for (const Expected& expected : cataclysms()) {
        const Axiom* found = set.find(expected.id);
        REQUIRE(found != nullptr);
        const Axiom& axiom = *found;
        for (const std::string& other : axiom.conflicts) {
            CAPTURE(axiom.id, other);
            REQUIRE(other != axiom.id);
            REQUIRE(other.rfind("cataclysm.", 0) == 0);
        }
    }
}

TEST_CASE("Artemis Withdrew removes fauna, hunting, hides and mounts rather than adding content")
{
    AxiomSet set;
    REQUIRE(set.loadDirectory(ICHOR_REPO_DATA));
    REQUIRE(set.find("cataclysm.artemis_withdrew") != nullptr);
    const Axiom& artemis = *set.find("cataclysm.artemis_withdrew");
    const std::vector<std::string>& removed = words(artemis, "enemies", "roster_remove");
    REQUIRE(has(removed, "wolf"));
    REQUIRE(has(removed, "horse"));
    REQUIRE(artemis.blocks.at("enemies").count("roster_add") == 0);
    const std::vector<std::string>& scarce = words(artemis, "economy", "scarce");
    REQUIRE(has(scarce, "hides"));
    REQUIRE(has(scarce, "mounts"));
    const auto& weights = std::get<std::map<std::string, double, std::less<>>>(artemis.blocks.at("poi").at("weights"));
    REQUIRE(weights.at("hunting_lodge") == 0.0);
    REQUIRE(artemis.blocks.count("traversal") == 0);
}

TEST_CASE("the Silence keeps conventional traversal and swaps healing from prayer to pharmaka")
{
    AxiomSet set;
    REQUIRE(set.loadDirectory(ICHOR_REPO_DATA));
    REQUIRE(set.find("cataclysm.silence_of_the_oracles") != nullptr);
    const Axiom& silence = *set.find("cataclysm.silence_of_the_oracles");
    REQUIRE(has(words(silence, "economy", "abundant"), "pharmaka"));
    REQUIRE(has(words(silence, "economy", "scarce"), "votive_offerings"));
    const auto& weights = std::get<std::map<std::string, double, std::less<>>>(silence.blocks.at("poi").at("weights"));
    REQUIRE(weights.at("dead_sanctuary") > 1.0);
    REQUIRE(silence.blocks.count("traversal") == 0);
}
