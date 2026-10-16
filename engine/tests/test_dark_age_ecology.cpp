#include "ichor/factions/dark_age_ecology.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ichor;

namespace {

Faction f(const std::string& id, FactionRole role)
{
    return Faction{id, id, role};
}

std::vector<Faction> world(bool incumbent)
{
    std::vector<Faction> set;
    if (incumbent) {
        set.push_back(f("inc", FactionRole::Incumbent));
    }
    set.push_back(f("ch", FactionRole::Challenger));
    set.push_back(f("pa", FactionRole::Parasite));
    set.push_back(f("cu", FactionRole::Custodian));
    set.push_back(f("re", FactionRole::Refusenik));
    set.push_back(f("he", FactionRole::Herald));
    return set;
}

}

TEST_CASE("a normal Powers axiom keeps an incumbent and normal expansion", "[factions][dark_age]")
{
    for (const char* powers : {"powers.hegemony", "powers.league", "powers.satrapy", "powers.amphictyony", "powers.poleis"}) {
        const auto plan = planEcology(powers, "law.oath");
        REQUIRE(plan.has_value());
        CHECK_FALSE(plan->postState);
        CHECK(plan->role(FactionRole::Incumbent).minCount == 1);
        CHECK(plan->role(FactionRole::Parasite).expansionPermille == kNormalExpansionPermille);
        CHECK(plan->role(FactionRole::Refusenik).expansionPermille == kNormalExpansionPermille);
        CHECK(checkAgainstEcology(*plan, world(true)).empty());
        CHECK_FALSE(checkAgainstEcology(*plan, world(false)).empty());
    }
}

TEST_CASE("the Dark Age has no incumbent and parasites and refuseniks expand", "[factions][dark_age]")
{
    const auto plan = planEcology("powers.dark_age", "law.oath");
    REQUIRE(plan.has_value());
    CHECK(plan->postState);
    CHECK(plan->role(FactionRole::Incumbent).maxCount == 0);
    CHECK(plan->role(FactionRole::Parasite).expansionPermille == kDarkAgeExpansionPermille);
    CHECK(plan->role(FactionRole::Refusenik).expansionPermille == kDarkAgeExpansionPermille);
    CHECK(plan->role(FactionRole::Herald).expansionPermille == kNormalExpansionPermille);
    CHECK(plan->role(FactionRole::Challenger).minCount == 1);
    CHECK(checkAgainstEcology(*plan, world(false)).empty());

    const auto problems = checkAgainstEcology(*plan, world(true));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("no incumbent") != std::string::npos);
}

TEST_CASE("territory weights differ from the normal world only for the vacuum roles", "[factions][dark_age]")
{
    const auto set = world(false);
    const auto dark = territoryWeightsPermille(*planEcology("powers.dark_age", "law.oath"), set);
    const auto normal = territoryWeightsPermille(*planEcology("powers.league", "law.oath"), set);
    REQUIRE(dark.size() == set.size());
    REQUIRE(normal.size() == set.size());
    for (std::size_t i = 0; i < set.size(); ++i) {
        const bool expands = set[i].role == FactionRole::Parasite || set[i].role == FactionRole::Refusenik;
        CHECK(dark[i] == (expands ? kDarkAgeExpansionPermille : kNormalExpansionPermille));
        CHECK(normal[i] == kNormalExpansionPermille);
    }
}

TEST_CASE("mixed axioms: the Law axiom never changes the ecology shape", "[factions][dark_age]")
{
    const auto base = planEcology("powers.dark_age", "law.oath");
    REQUIRE(base.has_value());
    for (const char* law : {"law.orichalcum", "law.hecatomb", "law.mysteries", "law.temenos", "law.last_favour", "law.oath"}) {
        const auto dark = planEcology("powers.dark_age", law);
        REQUIRE(dark.has_value());
        for (std::size_t i = 0; i < kFactionRoleCount; ++i) {
            CHECK(dark->roles[i].minCount == base->roles[i].minCount);
            CHECK(dark->roles[i].maxCount == base->roles[i].maxCount);
            CHECK(dark->roles[i].expansionPermille == base->roles[i].expansionPermille);
        }
        CHECK(planEcology("powers.hegemony", law)->role(FactionRole::Parasite).expansionPermille == kNormalExpansionPermille);
    }
}

TEST_CASE("empty input", "[factions][dark_age]")
{
    CHECK_FALSE(planEcology("", "").has_value());
    CHECK_FALSE(planEcology("powers.dark_age", "").has_value());
    CHECK_FALSE(planEcology("", "law.oath").has_value());

    const auto plan = *planEcology("powers.dark_age", "law.oath");
    CHECK(territoryWeightsPermille(plan, {}).empty());
    CHECK(checkAgainstEcology(plan, {}).size() == 5);
}

TEST_CASE("hostile ids are rejected without crashing", "[factions][dark_age]")
{
    CHECK_FALSE(planEcology("powers.", "law.oath").has_value());
    CHECK_FALSE(planEcology("powers.dark_age ", "law.oath").has_value());
    CHECK_FALSE(planEcology("Powers.dark_age", "law.oath").has_value());
    CHECK_FALSE(planEcology("law.oath", "powers.dark_age").has_value());
    CHECK_FALSE(planEcology("powers.dark-age", "law.oath").has_value());
    CHECK_FALSE(planEcology(std::string("powers.dark_age\0x", 16), "law.oath").has_value());
    CHECK_FALSE(planEcology("powers.\xE2\x80\x94", "law.oath").has_value());
    CHECK_FALSE(planEcology("powers.dark_age", std::string("law.") + std::string(10000, 'a')).has_value());
    CHECK_FALSE(planEcology(std::string("powers.") + std::string(1 << 20, 'a'), "law.oath").has_value());
    CHECK(planEcology("powers.unknown_future", "law.oath").has_value());
}

TEST_CASE("surplus roles in a set are counted, not trusted", "[factions][dark_age]")
{
    auto set = world(false);
    set.push_back(f("ch2", FactionRole::Challenger));
    set.push_back(f("ch3", FactionRole::Challenger));
    CHECK_FALSE(checkAgainstEcology(*planEcology("powers.dark_age", "law.oath"), set).empty());
}
