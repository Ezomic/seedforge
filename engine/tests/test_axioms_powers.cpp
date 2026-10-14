#include "ichor/data/axiom.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace ichor;

namespace {

struct Expected {
    const char* id;
    const char* file;
    const char* title;
    std::vector<const char*> blocks;
};

const std::vector<Expected>& powers()
{
    static const std::vector<Expected> list = {
        {"powers.league", "league.json", "The League", {"settlement", "poi", "enemies", "economy", "palette", "audio"}},
        {"powers.satrapy", "satrapy.json", "The Satrapy", {"settlement", "poi", "enemies", "economy", "palette", "audio"}},
        {"powers.hegemony", "hegemony.json", "The Hegemony", {"settlement", "poi", "enemies", "economy", "palette", "audio"}},
    };
    return list;
}

std::string readAxiomFile(const char* file)
{
    std::ifstream stream(std::filesystem::path(ICHOR_REPO_DATA) / "axioms" / file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

const AxiomBlock& block(const Axiom& axiom, const std::string& name)
{
    return axiom.blocks.at(name);
}

}

TEST_CASE("the three Powers axioms load through the real loader")
{
    AxiomSet set;
    const bool ok = set.loadDirectory(ICHOR_REPO_DATA);
    INFO((set.problems().empty() ? std::string() : set.problems().front()));
    REQUIRE(ok);
    for (const Expected& want : powers()) {
        const Axiom* axiom = set.find(want.id);
        INFO(want.id);
        REQUIRE(axiom != nullptr);
        REQUIRE(axiom->slot == AxiomSlot::Powers);
        REQUIRE(axiom->title == want.title);
        REQUIRE_FALSE(axiom->tags.empty());
        REQUIRE(axiom->blocks.size() >= 4);
        for (const char* name : want.blocks) {
            INFO(name);
            REQUIRE(axiom->blocks.count(name) == 1);
        }
        REQUIRE(axiom->conflicts.end() == std::find(axiom->conflicts.begin(), axiom->conflicts.end(), want.id));
    }
}

TEST_CASE("the Powers files carry no em-dash and no placeholder text")
{
    for (const Expected& want : powers()) {
        const std::string text = readAxiomFile(want.file);
        INFO(want.file);
        REQUIRE_FALSE(text.empty());
        REQUIRE(text.find("\xE2\x80\x94") == std::string::npos);
        REQUIRE(text.find("--") == std::string::npos);
        for (const char* placeholder : {"TODO", "TBD", "FIXME", "placeholder", "lorem"}) {
            REQUIRE(text.find(placeholder) == std::string::npos);
        }
    }
}

TEST_CASE("the Powers axioms keep to the settled value ranges")
{
    AxiomSet set;
    REQUIRE(set.loadDirectory(ICHOR_REPO_DATA));
    for (const Expected& want : powers()) {
        const Axiom& axiom = *set.find(want.id);
        INFO(want.id);
        const double density = std::get<double>(block(axiom, "settlement").at("density_mult"));
        REQUIRE(density >= 0.5);
        REQUIRE(density <= 1.5);
        for (const auto& [name, weight] : std::get<std::map<std::string, double, std::less<>>>(block(axiom, "poi").at("weights"))) {
            INFO(name);
            REQUIRE(weight > 0.0);
            REQUIRE(weight <= 5.0);
        }
    }
}

TEST_CASE("each Powers axiom carries the consequences doc 01 names")
{
    AxiomSet set;
    REQUIRE(set.loadDirectory(ICHOR_REPO_DATA));
    const auto words = [](const Axiom& axiom, const char* blockName, const char* key) {
        return std::get<std::vector<std::string>>(block(axiom, blockName).at(key));
    };
    const auto has = [](const std::vector<std::string>& list, const std::string& item) { return std::find(list.begin(), list.end(), item) != list.end(); };
    const auto weights = [](const Axiom& axiom) { return std::get<std::map<std::string, double, std::less<>>>(block(axiom, "poi").at("weights")); };

    const Axiom& league = *set.find("powers.league");
    REQUIRE(has(words(league, "enemies", "roster_add"), "contract_sellsword"));
    REQUIRE(has(words(league, "economy", "abundant"), "silver"));
    REQUIRE(weights(league).count("harbour_emporium") == 1);
    REQUIRE(has(words(league, "traversal", "unlock"), "boat"));

    const Axiom& satrapy = *set.find("powers.satrapy");
    REQUIRE(weights(satrapy).count("collaborator_estate") == 1);
    REQUIRE(weights(satrapy).count("resistance_hideout") == 1);
    REQUIRE(weights(satrapy).count("garrison_fort") == 1);

    const Axiom& hegemony = *set.find("powers.hegemony");
    REQUIRE(weights(hegemony).count("ruined_frontier_fort") == 1);
    REQUIRE(weights(hegemony).count("imperial_road_station") == 1);
    REQUIRE(std::get<double>(block(hegemony, "settlement").at("density_mult")) < std::get<double>(block(league, "settlement").at("density_mult")));
}
