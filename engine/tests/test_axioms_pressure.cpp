#include "ichor/data/axiom.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

using Weights = std::map<std::string, double, std::less<>>;
using Words = std::vector<std::string>;

const AxiomSet& shipped()
{
    static const AxiomSet set = [] {
        AxiomSet loaded;
        const bool ok = loaded.loadDirectory(ICHOR_REPO_DATA);
        INFO((loaded.problems().empty() ? std::string() : loaded.problems().front()));
        REQUIRE(ok);
        return loaded;
    }();
    return set;
}

const Axiom& axiom(const std::string& id)
{
    const Axiom* found = shipped().find(id);
    REQUIRE(found != nullptr);
    return *found;
}

bool has(const Words& words, const std::string& word)
{
    return std::find(words.begin(), words.end(), word) != words.end();
}

const Words& words(const Axiom& a, const std::string& block, const std::string& key)
{
    return std::get<Words>(a.blocks.at(block).at(key));
}

const Weights& poiWeights(const Axiom& a)
{
    return std::get<Weights>(a.blocks.at("poi").at("weights"));
}

}

TEST_CASE("the three shipped Pressure axioms validate and use the pressure slot")
{
    const std::vector<std::pair<std::string, std::string>> expected = {
        {"pressure.miasma", "The Miasma"},
        {"pressure.unburied", "The Unburied"},
        {"pressure.foretold", "The Foretold"},
    };
    for (const auto& [id, title] : expected) {
        const Axiom& a = axiom(id);
        REQUIRE(a.slot == AxiomSlot::Pressure);
        REQUIRE(a.title == title);
        REQUIRE_FALSE(a.tags.empty());
        REQUIRE(a.blocks.size() >= 4);
    }
    const auto pressure = std::count_if(shipped().axioms().begin(), shipped().axioms().end(), [](const auto& entry) { return entry.second.slot == AxiomSlot::Pressure; });
    REQUIRE(pressure >= 3);
}

TEST_CASE("no Pressure data file carries an em-dash or placeholder text")
{
    for (const char* name : {"miasma", "unburied", "foretold"}) {
        std::ifstream in(std::filesystem::path(ICHOR_REPO_DATA) / "axioms" / (std::string(name) + ".json"), std::ios::binary);
        REQUIRE(in.good());
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE_FALSE(text.empty());
        REQUIRE(text.find("â") == std::string::npos);
        for (const char* placeholder : {"TODO", "TBD", "placeholder", "lorem", "FIXME"}) {
            REQUIRE(text.find(placeholder) == std::string::npos);
        }
    }
}

TEST_CASE("the three Pressure axioms are distinct")
{
    std::set<std::uint64_t> digests;
    std::set<std::string> grades;
    for (const char* id : {"pressure.miasma", "pressure.unburied", "pressure.foretold"}) {
        const Axiom& a = axiom(id);
        digests.insert(axiomDigest(a));
        REQUIRE(a.blocks.count("audio") == 1);
        grades.insert(std::get<std::string>(a.blocks.at("palette").at("grade")));
    }
    REQUIRE(digests.size() == 3);
    REQUIRE(grades.size() == 3);
}

TEST_CASE("the Miasma is purified rather than fought")
{
    const Axiom& a = axiom("pressure.miasma");
    REQUIRE(poiWeights(a).at("purification_spring") >= 2.0);
    REQUIRE(poiWeights(a).at("polluted_well") >= 2.0);
    REQUIRE(has(words(a, "economy", "abundant"), "sulphur"));
    REQUIRE(has(words(a, "economy", "scarce"), "clean_water"));
    REQUIRE(has(words(a, "biomes", "inject"), "miasma_fen"));
    REQUIRE(has(words(a, "enemies", "roster_add"), "afflicted"));
    REQUIRE(poiWeights(a).at("purification_spring") > poiWeights(a).at("farmstead"));
    REQUIRE(poiWeights(a).at("purification_spring") > poiWeights(a).at("polluted_well"));
}

TEST_CASE("the Unburied raises the dead and the graves that hold them")
{
    const Axiom& a = axiom("pressure.unburied");
    REQUIRE(has(words(a, "enemies", "roster_add"), "restless_dead"));
    REQUIRE(has(words(a, "enemies", "roster_add"), "shade"));
    REQUIRE(poiWeights(a).at("unmarked_grave") >= 2.0);
    REQUIRE(has(words(a, "biomes", "inject"), "unquiet_barrow"));
    REQUIRE(has(words(a, "economy", "abundant"), "grave_goods"));
}

TEST_CASE("the Foretold is a conqueror who is winning")
{
    const Axiom& a = axiom("pressure.foretold");
    REQUIRE(has(words(a, "enemies", "roster_add"), "conqueror_levy"));
    REQUIRE(poiWeights(a).at("war_camp") >= 2.0);
    REQUIRE(poiWeights(a).at("omen_stone") > 0.0);
    REQUIRE(has(words(a, "biomes", "inject"), "scorched_road"));
    REQUIRE(has(words(a, "economy", "scarce"), "grain"));
    REQUIRE(poiWeights(a).at("war_camp") > poiWeights(a).at("farmstead"));
    REQUIRE(poiWeights(a).at("burned_farmstead") > poiWeights(a).at("farmstead"));
}
