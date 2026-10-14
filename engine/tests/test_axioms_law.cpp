#include "ichor/data/axiom.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace ichor;

namespace {

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

const Axiom& law(const std::string& id)
{
    const Axiom* axiom = shipped().find(id);
    REQUIRE(axiom != nullptr);
    return *axiom;
}

bool hasBlock(const Axiom& axiom, const std::string& block, const std::string& key)
{
    const auto it = axiom.blocks.find(block);
    return it != axiom.blocks.end() && it->second.find(key) != it->second.end();
}

const std::vector<std::string> kLawIds = {"law.hecatomb", "law.orichalcum", "law.last_favour"};

}

TEST_CASE("the three shipped Law axioms exist in slot B")
{
    for (const std::string& id : kLawIds) {
        const Axiom& axiom = law(id);
        REQUIRE(axiom.slot == AxiomSlot::Law);
        REQUIRE(axiomSlotLetter(axiom.slot) == 'B');
        REQUIRE(axiom.id == id);
    }
    const auto inLaw = std::count_if(shipped().axioms().begin(), shipped().axioms().end(), [](const auto& entry) { return entry.second.slot == AxiomSlot::Law; });
    REQUIRE(inLaw >= 3);
}

TEST_CASE("each Law axiom carries titles, tags and at least four directive blocks")
{
    const std::vector<std::pair<std::string, std::string>> titles = {{"law.hecatomb", "Hecatomb"}, {"law.orichalcum", "Orichalcum"}, {"law.last_favour", "The Last Favour"}};
    for (const auto& [id, title] : titles) {
        const Axiom& axiom = law(id);
        REQUIRE(axiom.title == title);
        REQUIRE(axiom.tags.size() >= 3);
        REQUIRE(axiom.blocks.size() >= 4);
        for (const char* block : {"poi", "enemies", "economy", "palette", "audio"}) {
            REQUIRE(axiom.blocks.count(block) == 1);
        }
    }
}

TEST_CASE("Law axioms never name themselves or a Law sibling as a conflict")
{
    for (const std::string& id : kLawIds) {
        for (const std::string& other : law(id).conflicts) {
            REQUIRE(other != id);
            REQUIRE(other.rfind("law.", 0) != 0);
        }
    }
}

TEST_CASE("Orichalcum puts the ore on the ridges and makes mines the places to go")
{
    const Axiom& axiom = law("law.orichalcum");
    const auto& weights = std::get<std::map<std::string, double, std::less<>>>(axiom.blocks.at("poi").at("weights"));
    REQUIRE(weights.at("orichalcum_mine") > 1.0);
    REQUIRE(weights.at("ridge_fortress") > 1.0);
    const auto& inject = std::get<std::vector<std::string>>(axiom.blocks.at("biomes").at("inject"));
    REQUIRE(std::find(inject.begin(), inject.end(), "volcanic_ridge") != inject.end());
    const auto& abundant = std::get<std::vector<std::string>>(axiom.blocks.at("economy").at("abundant"));
    REQUIRE(std::find(abundant.begin(), abundant.end(), "refined_charge") != abundant.end());
}

TEST_CASE("Hecatomb makes bottled vitality the coin and healing scarce")
{
    const Axiom& axiom = law("law.hecatomb");
    const auto& abundant = std::get<std::vector<std::string>>(axiom.blocks.at("economy").at("abundant"));
    REQUIRE(std::find(abundant.begin(), abundant.end(), "bottled_vitality") != abundant.end());
    REQUIRE(hasBlock(axiom, "economy", "scarce"));
    const auto& weights = std::get<std::map<std::string, double, std::less<>>>(axiom.blocks.at("poi").at("weights"));
    REQUIRE(weights.at("hekatombaion_altar") > 1.0);
}

TEST_CASE("The Last Favour is deflationary: charges scarce, dead shrines and treasuries common")
{
    const Axiom& axiom = law("law.last_favour");
    const auto& scarce = std::get<std::vector<std::string>>(axiom.blocks.at("economy").at("scarce"));
    REQUIRE(std::find(scarce.begin(), scarce.end(), "divine_charge") != scarce.end());
    const auto& weights = std::get<std::map<std::string, double, std::less<>>>(axiom.blocks.at("poi").at("weights"));
    REQUIRE(weights.at("cold_altar") > 1.0);
    REQUIRE(weights.at("thesauros") > 1.0);
}

TEST_CASE("the Law axioms differ from each other in every identity block")
{
    for (const char* block : {"poi", "enemies", "economy", "palette", "audio"}) {
        for (std::size_t i = 0; i < kLawIds.size(); ++i) {
            for (std::size_t j = i + 1; j < kLawIds.size(); ++j) {
                REQUIRE(law(kLawIds[i]).blocks.at(block) != law(kLawIds[j]).blocks.at(block));
            }
        }
    }
}

TEST_CASE("Law axiom text carries no placeholder wording")
{
    for (const std::string& id : kLawIds) {
        const Axiom& axiom = law(id);
        std::string everything = axiomCanonicalJson(axiom);
        std::transform(everything.begin(), everything.end(), everything.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const char* banned : {"todo", "tbd", "placeholder", "lorem", "xxx", "fixme"}) {
            REQUIRE(everything.find(banned) == std::string::npos);
        }
    }
}
