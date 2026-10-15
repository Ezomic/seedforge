#include "ichor/factions/incompatible_wants.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace ichor;

namespace {

Faction make(const std::string& id) { return Faction{id, "The " + id, FactionRole::Incumbent}; }

std::vector<Faction> three() { return {make("a"), make("b"), make("c")}; }

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

}

TEST_CASE("differing stances on one axis are an incompatible pair", "[factions][incompatible]")
{
    const auto pairs = incompatiblePairs(three(), {{"b", "law", "free_access"}, {"a", "law", "control"}});
    REQUIRE(pairs.size() == 1);
    CHECK(pairs[0].axis == "law");
    CHECK(pairs[0].factionA == "a");
    CHECK(pairs[0].stanceA == "control");
    CHECK(pairs[0].factionB == "b");
    CHECK(pairs[0].stanceB == "free_access");
    CHECK(validateIncompatibleWants(three(), {{"a", "law", "control"}, {"b", "law", "free_access"}}).empty());
}

TEST_CASE("agreement, other axes and a lone want are not incompatible", "[factions][incompatible]")
{
    CHECK(incompatiblePairs(three(), {{"a", "law", "control"}, {"b", "law", "control"}}).empty());
    CHECK(incompatiblePairs(three(), {{"a", "law", "control"}, {"b", "order", "replace"}}).empty());
    CHECK(incompatiblePairs(three(), {{"a", "law", "control"}}).empty());
    CHECK(mentions(validateIncompatibleWants(three(), {{"a", "law", "control"}, {"b", "law", "control"}}), "incompatible"));
}

TEST_CASE("hasIncompatiblePair agrees with the pair list", "[factions][incompatible]")
{
    CHECK_FALSE(hasIncompatiblePair(three(), {}));
    CHECK_FALSE(hasIncompatiblePair(three(), {{"a", "law", "control"}, {"b", "law", "control"}}));
    CHECK_FALSE(hasIncompatiblePair(three(), {{"a", "law", "control"}, {"ghost", "law", "renounce"}}));
    CHECK(hasIncompatiblePair(three(), {{"a", "law", "control"}, {"b", "law", "renounce"}}));
}

TEST_CASE("empty inputs fail the rule without crashing", "[factions][incompatible]")
{
    CHECK(incompatiblePairs({}, {}).empty());
    CHECK(incompatiblePairs(three(), {}).empty());
    CHECK_FALSE(validateIncompatibleWants({}, {}).empty());
    CHECK_FALSE(validateIncompatibleWants(three(), {}).empty());
}

TEST_CASE("duplicate wants and one faction's two stances never pair it with itself", "[factions][incompatible]")
{
    CHECK(incompatiblePairs(three(), {{"a", "law", "control"}, {"a", "law", "control"}}).empty());
    CHECK(incompatiblePairs(three(), {{"a", "law", "control"}, {"a", "law", "renounce"}}).empty());
    const auto pairs = incompatiblePairs(three(),
        {{"a", "law", "control"}, {"a", "law", "control"}, {"a", "law", "renounce"}, {"b", "law", "control"}});
    REQUIRE(pairs.size() == 1);
    CHECK(pairs[0].factionA == "a");
    CHECK(pairs[0].stanceA == "renounce");
    CHECK(pairs[0].factionB == "b");
}

TEST_CASE("unknown faction ids are reported and never pair", "[factions][incompatible]")
{
    const std::vector<FactionWant> wants = {{"a", "law", "control"}, {"ghost", "law", "renounce"}};
    CHECK(incompatiblePairs(three(), wants).empty());
    const auto problems = validateIncompatibleWants(three(), wants);
    CHECK(mentions(problems, "ghost"));
}

TEST_CASE("empty axis or stance is reported", "[factions][incompatible]")
{
    CHECK(mentions(validateIncompatibleWants(three(), {{"a", "", "control"}, {"b", "law", "x"}}), "axis"));
    CHECK(mentions(validateIncompatibleWants(three(), {{"a", "law", ""}, {"b", "law", "x"}}), "stance"));
    CHECK(incompatiblePairs(three(), {{"a", "law", ""}, {"b", "law", "x"}}).empty());
}

TEST_CASE("pairs are ordered whatever the input order", "[factions][incompatible]")
{
    std::vector<FactionWant> wants = {{"c", "order", "replace"}, {"a", "order", "restore"}, {"b", "law", "renounce"}, {"a", "law", "control"}};
    const auto first = incompatiblePairs(three(), wants);
    std::reverse(wants.begin(), wants.end());
    const auto second = incompatiblePairs(three(), wants);
    REQUIRE(first.size() == 2);
    REQUIRE(second.size() == 2);
    CHECK(hasIncompatiblePair(three(), wants));
    CHECK(first[0].axis == "law");
    CHECK(first[0].factionA == "a");
    CHECK(first[0].stanceA == "control");
    CHECK(first[0].factionB == "b");
    CHECK(first[0].stanceB == "renounce");
    CHECK(first[1].factionA == "a");
    CHECK(first[1].stanceA == "restore");
    CHECK(first[1].factionB == "c");
    CHECK(first[1].axis == "order");
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].axis == second[i].axis);
        CHECK(first[i].factionA == second[i].factionA);
        CHECK(first[i].factionB == second[i].factionB);
    }
}

TEST_CASE("duplicate faction ids in the set do not double a pair", "[factions][incompatible]")
{
    const std::vector<Faction> dup = {make("a"), make("a"), make("b")};
    CHECK(incompatiblePairs(dup, {{"a", "law", "control"}, {"b", "law", "renounce"}}).size() == 1);
}

TEST_CASE("many factions on one axis pair every cross-faction combination", "[factions][incompatible]")
{
    std::vector<Faction> factions;
    std::vector<FactionWant> wants;
    for (int i = 0; i < 40; ++i) {
        factions.push_back(make("f" + std::to_string(100 + i)));
        wants.push_back({"f" + std::to_string(100 + i), "law", "s" + std::to_string(i)});
    }
    const auto pairs = incompatiblePairs(factions, wants);
    CHECK(pairs.size() == 40u * 39u / 2u);
    CHECK(hasIncompatiblePair(factions, wants));
    CHECK(std::is_sorted(pairs.begin(), pairs.end(), [](const IncompatiblePair& l, const IncompatiblePair& r) {
        return l.factionA != r.factionA ? l.factionA < r.factionA : l.factionB < r.factionB;
    }));
}
