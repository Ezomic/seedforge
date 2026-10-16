#include "ichor/data/axiom.h"
#include "ichor/ui/law_hud_config.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace ichor;

namespace {

const std::filesystem::path kRepoData = ICHOR_REPO_DATA;

LawHudConfig shipped()
{
    std::ifstream file(kRepoData / "ui" / "law_hud.json", std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const LawHudConfigResult result = parseLawHudConfig(text.str());
    REQUIRE(result.ok());
    return result.config;
}

bool has(const std::vector<std::string>& list, const std::string& id)
{
    return std::find(list.begin(), list.end(), id) != list.end();
}

}

TEST_CASE("every Law in data/axioms has its own HUD configuration", "[law_hud]")
{
    const LawHudConfig config = shipped();
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(kRepoData));
    std::size_t laws = 0;
    for (const auto& [id, axiom] : axioms.axioms()) {
        if (axiom.slot != AxiomSlot::Law) {
            continue;
        }
        ++laws;
        REQUIRE(config.laws.count(id) == 1);
        REQUIRE(lawHudElements(config, id) != config.base);
    }
    REQUIRE(laws >= 5);
}

TEST_CASE("each Law shows the element doc 13 gives it and none of the others", "[law_hud]")
{
    const LawHudConfig config = shipped();
    const std::pair<std::string, std::string> expected[] = {
        {"law.hecatomb", "vitality_cost"},
        {"law.oath", "debt_meter"},
        {"law.last_favour", "favour_count"},
        {"law.temenos", "consecrated_ground"},
        {"law.orichalcum", "metal_supply"},
        {"law.mysteries", "rite_loadout"},
    };
    std::set<std::string> distinct;
    for (const auto& [law, element] : expected) {
        const std::vector<std::string> shown = lawHudElements(config, law);
        REQUIRE(has(shown, element));
        distinct.insert(element);
        for (const auto& [other, otherElement] : expected) {
            if (other != law) {
                REQUIRE_FALSE(has(shown, otherElement));
            }
        }
    }
    REQUIRE(distinct.size() == 6);
}

TEST_CASE("Hecatomb replaces the health bar with vitality and the rest keep it", "[law_hud]")
{
    const LawHudConfig config = shipped();
    const auto hecatomb = lawHudElements(config, "law.hecatomb");
    REQUIRE_FALSE(has(hecatomb, "health"));
    REQUIRE(hecatomb.size() == config.base.size());
    REQUIRE(has(lawHudElements(config, "law.oath"), "health"));
}

TEST_CASE("an unknown, empty or non-Law id gets the base HUD", "[law_hud]")
{
    const LawHudConfig config = shipped();
    REQUIRE(config.base == std::vector<std::string>{"compass", "clock", "water", "health"});
    for (const char* id : {"", "law.nonsense", "flood", "LAW.OATH", "law.oath ", "law."}) {
        REQUIRE(lawHudElements(config, id) == config.base);
    }
    REQUIRE(lawHudElements(LawHudConfig{}, "law.oath").empty());
}

TEST_CASE("no shipped list repeats an element or names an unknown one", "[law_hud]")
{
    const LawHudConfig config = shipped();
    auto check = [](const std::vector<std::string>& list) {
        REQUIRE(std::set<std::string>(list.begin(), list.end()).size() == list.size());
        for (const std::string& id : list) {
            REQUIRE(isLawHudElementId(id));
        }
    };
    check(config.base);
    for (const auto& [law, list] : config.laws) {
        check(list);
    }
}

TEST_CASE("a hostile config is reported and never crashes", "[law_hud]")
{
    const char* bad[] = {
        "",
        "not json",
        "[]",
        "null",
        "{}",
        "{\"base\":[]}",
        "{\"laws\":{}}",
        "{\"base\":\"compass\",\"laws\":{}}",
        "{\"base\":[],\"laws\":[]}",
        "{\"base\":[\"compass\",\"compass\"],\"laws\":{}}",
        "{\"base\":[],\"laws\":{\"law.x\":[\"nope\"]}}",
        "{\"base\":[],\"laws\":{\"law.x\":[1]}}",
        "{\"base\":[],\"laws\":{\"law.x\":\"health\"}}",
        "{\"base\":[],\"laws\":{\"law.x\":[\"health\",\"health\"]}}",
        "{\"base\":[null],\"laws\":{}}",
    };
    for (const char* text : bad) {
        const LawHudConfigResult result = parseLawHudConfig(text);
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.config.base.empty());
        REQUIRE(result.config.laws.empty());
    }
}

TEST_CASE("a flood of faults is capped", "[law_hud]")
{
    std::string text = "{\"base\":[],\"laws\":{";
    for (int i = 0; i < 100; ++i) {
        text += (i ? "," : "") + std::string("\"law.") + std::to_string(i) + "\":[\"nope\"]";
    }
    text += "}}";
    const LawHudConfigResult result = parseLawHudConfig(text);
    REQUIRE(result.problems.size() == kMaxLawHudProblems + 1);
    REQUIRE(result.problems.back() == "and 84 more");
}
