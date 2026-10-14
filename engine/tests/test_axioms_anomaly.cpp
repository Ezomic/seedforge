#include "ichor/data/axiom.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::vector<std::string> kAnomalyIds = {"anomaly.labyrinth", "anomaly.tiresias", "anomaly.underworld"};

const AxiomSet& shipped()
{
    static AxiomSet set;
    static const bool loaded = set.loadDirectory(ICHOR_REPO_DATA);
    INFO((set.problems().empty() ? std::string() : set.problems().front()));
    REQUIRE(loaded);
    return set;
}

const Axiom& anomaly(const std::string& id)
{
    const Axiom* axiom = shipped().find(id);
    REQUIRE(axiom != nullptr);
    return *axiom;
}

bool hasTag(const Axiom& axiom, const std::string& tag)
{
    return std::find(axiom.tags.begin(), axiom.tags.end(), tag) != axiom.tags.end();
}

const std::vector<std::string>& words(const Axiom& axiom, const std::string& block, const std::string& key)
{
    return std::get<std::vector<std::string>>(axiom.blocks.at(block).at(key));
}

double weight(const Axiom& axiom, const std::string& key)
{
    return std::get<std::map<std::string, double, std::less<>>>(axiom.blocks.at("poi").at("weights")).at(key);
}

bool contains(const std::vector<std::string>& list, const std::string& word)
{
    return std::find(list.begin(), list.end(), word) != list.end();
}

}

TEST_CASE("three anomaly axioms ship in slot E and validate through the real loader")
{
    for (const std::string& id : kAnomalyIds) {
        const Axiom& axiom = anomaly(id);
        REQUIRE(axiom.slot == AxiomSlot::Anomaly);
        REQUIRE(axiomSlotLetter(axiom.slot) == 'E');
        REQUIRE_FALSE(axiom.title.empty());
        REQUIRE_FALSE(axiom.tags.empty());
        REQUIRE(axiom.blocks.size() >= 4);
    }
    const auto anomalies = std::count_if(shipped().axioms().begin(), shipped().axioms().end(), [](const auto& entry) { return entry.second.slot == AxiomSlot::Anomaly; });
    REQUIRE(anomalies >= 3);
}

TEST_CASE("anomaly conflicts name only well-formed other-slot ids")
{
    for (const std::string& id : kAnomalyIds) {
        for (const std::string& other : anomaly(id).conflicts) {
            REQUIRE(other != id);
            REQUIRE(other.rfind("anomaly.", 0) != 0);
        }
    }
}

TEST_CASE("the labyrinth is a real place on the map with its monster and a way through")
{
    const Axiom& axiom = anomaly("anomaly.labyrinth");
    REQUIRE(axiom.title == "The Labyrinth");
    REQUIRE(hasTag(axiom, "LABYRINTH"));
    REQUIRE(weight(axiom, "labyrinth") > 1.0);
    REQUIRE(contains(words(axiom, "enemies", "roster_add"), "minotaur"));
    REQUIRE(contains(words(axiom, "traversal", "unlock"), "ariadnes_thread"));
    REQUIRE(axiom.blocks.count("palette") == 1);
    REQUIRE(axiom.blocks.count("audio") == 1);
}

TEST_CASE("tiresias lives: a findable seer, not a fighting force")
{
    const Axiom& axiom = anomaly("anomaly.tiresias");
    REQUIRE(axiom.title == "Tiresias Lives");
    REQUIRE(hasTag(axiom, "PROPHECY"));
    REQUIRE(weight(axiom, "tiresias_hut") > 0.0);
    REQUIRE(axiom.blocks.count("enemies") == 0);
    REQUIRE(axiom.blocks.count("traversal") == 0);
}

TEST_CASE("the underworld is physically reachable: a gate, a ferry, the dead")
{
    const Axiom& axiom = anomaly("anomaly.underworld");
    REQUIRE(axiom.title == "The Underworld Is Reachable");
    REQUIRE(hasTag(axiom, "UNDERWORLD"));
    REQUIRE(weight(axiom, "underworld_gate") > 1.0);
    REQUIRE(contains(words(axiom, "traversal", "unlock"), "descend_underworld"));
    REQUIRE(contains(words(axiom, "enemies", "roster_add"), "shade"));
    REQUIRE(axiom.blocks.count("biomes") == 1);
}

TEST_CASE("anomaly files carry no em-dash and no placeholder text")
{
    for (const std::string name : {"labyrinth", "tiresias", "underworld"}) {
        const std::filesystem::path file = std::filesystem::path(ICHOR_REPO_DATA) / "axioms" / ("anomaly_" + name + ".json");
        std::ifstream stream(file, std::ios::binary);
        REQUIRE(stream.good());
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        REQUIRE(text.find("\xE2\x80\x94") == std::string::npos);
        for (const char* placeholder : {"TODO", "TBD", "FIXME", "lorem", "placeholder"}) {
            REQUIRE(text.find(placeholder) == std::string::npos);
        }
    }
}
