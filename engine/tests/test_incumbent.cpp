#include "ichor/factions/incumbent.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

void checkSingle(const std::string& axiom, const std::string& id, const std::string& name)
{
    const auto plan = planIncumbents(axiom);
    REQUIRE(plan.has_value());
    CHECK(plan->shape == IncumbentShape::Single);
    REQUIRE(plan->incumbents.size() == 1);
    CHECK(plan->incumbents[0].id == id);
    CHECK(plan->incumbents[0].name == name);
    CHECK(plan->incumbents[0].role == FactionRole::Incumbent);
}

}

TEST_CASE("each single-incumbent Powers axiom maps to its doc 03 faction", "[factions][incumbent]")
{
    checkSingle("powers.hegemony", "archonate", "The Archonate");
    checkSingle("powers.league", "hellenotamiai", "The Hellenotamiai");
    checkSingle("powers.satrapy", "garrison", "The Garrison");
    checkSingle("powers.amphictyony", "council", "The Council");
}

TEST_CASE("the Dark Age has no Incumbent", "[factions][incumbent]")
{
    const auto plan = planIncumbents("powers.dark_age");
    REQUIRE(plan.has_value());
    CHECK(plan->shape == IncumbentShape::None);
    CHECK(plan->incumbents.empty());
}

TEST_CASE("the Poleis run three or four Incumbents", "[factions][incumbent]")
{
    for (const int count : {3, 4}) {
        const auto plan = planIncumbents("powers.poleis", count);
        REQUIRE(plan.has_value());
        CHECK(plan->shape == IncumbentShape::Poleis);
        REQUIRE(plan->incumbents.size() == static_cast<std::size_t>(count));
        std::set<std::string> ids;
        std::set<std::string> names;
        for (const auto& f : plan->incumbents) {
            CHECK(f.role == FactionRole::Incumbent);
            ids.insert(f.id);
            names.insert(f.name);
        }
        CHECK(ids.size() == static_cast<std::size_t>(count));
        CHECK(names.size() == static_cast<std::size_t>(count));
    }
}

TEST_CASE("a Poleis count outside 3 to 4 is rejected", "[factions][incumbent]")
{
    for (const int count : {std::numeric_limits<int>::min(), -1, 0, 1, 2, 5, 100, std::numeric_limits<int>::max()}) {
        CHECK_FALSE(planIncumbents("powers.poleis", count).has_value());
    }
}

TEST_CASE("the Poleis count is ignored for other axioms", "[factions][incumbent]")
{
    const auto plan = planIncumbents("powers.league", -7);
    REQUIRE(plan.has_value());
    CHECK(plan->incumbents.size() == 1);
}

TEST_CASE("unknown, empty and non-Powers ids give no plan", "[factions][incumbent]")
{
    const std::vector<std::string> ids = {"", "powers.", "powers.unknown", "Powers.League", "powers.league ", "law.orichalcum", "hegemony",
        "cataclysm.flood", std::string("powers.league\0x", 14)};
    for (const auto& id : ids) {
        CHECK_FALSE(planIncumbents(id).has_value());
    }
    CHECK_FALSE(planIncumbents(std::string(1 << 20, 'a')).has_value());
}

TEST_CASE("planning is deterministic and incumbent ids are valid words", "[factions][incumbent]")
{
    for (const char* axiom : {"powers.hegemony", "powers.league", "powers.satrapy", "powers.amphictyony", "powers.poleis"}) {
        const auto a = planIncumbents(axiom, 4);
        const auto b = planIncumbents(axiom, 4);
        REQUIRE(a.has_value());
        CHECK(factionsDigest(a->incumbents) == factionsDigest(b->incumbents));
    }
}

TEST_CASE("a single Incumbent plus the other roles forms a valid set", "[factions][incumbent]")
{
    auto set = planIncumbents("powers.hegemony")->incumbents;
    set.push_back({"archaists", "The Archaists", FactionRole::Challenger});
    set.push_back({"metallarchs", "The Metallarchs", FactionRole::Parasite});
    set.push_back({"mnemones", "The Mnemones", FactionRole::Custodian});
    set.push_back({"asebeis", "The Asebeis", FactionRole::Refusenik});
    set.push_back({"kerykes", "The Kerykes", FactionRole::Herald});
    CHECK(validateFactionSet(set).empty());
}

TEST_CASE("the Dark Age set is valid without an Incumbent", "[factions][incumbent]")
{
    auto set = planIncumbents("powers.dark_age")->incumbents;
    set.push_back({"archaists", "The Archaists", FactionRole::Challenger});
    set.push_back({"metallarchs", "The Metallarchs", FactionRole::Parasite});
    set.push_back({"mnemones", "The Mnemones", FactionRole::Custodian});
    set.push_back({"asebeis", "The Asebeis", FactionRole::Refusenik});
    set.push_back({"kerykes", "The Kerykes", FactionRole::Herald});
    CHECK(validateFactionSet(set).empty());
}
