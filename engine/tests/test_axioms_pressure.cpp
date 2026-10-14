#include "ichor/data/axiom.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

TEST_CASE("no Pressure axiom carries an em-dash or placeholder text")
{
    for (const char* id : {"pressure.miasma", "pressure.unburied", "pressure.foretold"}) {
        const std::string text = axiomCanonicalJson(axiom(id));
        REQUIRE(text.find("\xE2\x80\x94") == std::string::npos);
        for (const char* placeholder : {"TODO", "TBD", "placeholder", "lorem", "FIXME", "xxx"}) {
            REQUIRE(text.find(placeholder) == std::string::npos);
        }
    }
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
}
