#include "ichor/data/data_set.h"
#include "ichor/economy/catalogue_distribution_logic.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::vector<CatalogueHome> kToy = {
    {"knife", {"powers.league", "law.last_favour"}},
    {"spear", {"law.hecatomb"}},
    {"orphan", {}},
    {"map", {"law.last_favour", "law.hecatomb", "powers.league"}},
};

std::map<std::string, std::uint32_t> byId(const std::vector<DistributedItem>& items)
{
    std::map<std::string, std::uint32_t> out;
    for (const DistributedItem& item : items) out[item.item] = item.weight;
    return out;
}

const DataTable* shippedCatalogue()
{
    static DataSet set;
    static const bool loaded = set.loadDirectory(ICHOR_REPO_DATA);
    REQUIRE(loaded);
    return set.table("catalogue");
}

std::set<std::string> everyHome(const std::vector<CatalogueHome>& homes)
{
    std::set<std::string> every;
    for (const CatalogueHome& h : homes) every.insert(h.homes.begin(), h.homes.end());
    return every;
}

}

TEST_CASE("one active axiom brings in exactly the items homed there")
{
    const auto got = byId(availableItems(kToy, {"law.hecatomb"}));
    REQUIRE(got == std::map<std::string, std::uint32_t>{{"spear", 1}, {"map", 1}});
}

TEST_CASE("mixed axioms weight an item by how many of its homes are active")
{
    const auto got = byId(availableItems(kToy, {"law.hecatomb", "powers.league", "law.last_favour"}));
    REQUIRE(got.at("knife") == 2);
    REQUIRE(got.at("spear") == 1);
    REQUIRE(got.at("map") == 3);
}

TEST_CASE("an item with no home never appears and an empty axiom set yields nothing")
{
    REQUIRE(!byId(availableItems(kToy, {"law.hecatomb", "powers.league"})).contains("orphan"));
    REQUIRE(availableItems(kToy, {}).empty());
    REQUIRE(availableItems({}, {"law.hecatomb"}).empty());
}

TEST_CASE("unknown axiom ids match nothing and duplicate homes or entries count once")
{
    REQUIRE(availableItems(kToy, {"no.such_axiom", ""}).empty());
    const std::vector<CatalogueHome> dupes = {{"a", {"x", "x", "x"}}, {"a", {"x"}}, {"", {"x"}}};
    const auto got = availableItems(dupes, {"x"});
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].weight == 1);
}

TEST_CASE("removing an axiom input changes the result")
{
    const auto with = availableItems(kToy, {"law.hecatomb", "powers.league"});
    const auto without = availableItems(kToy, {"powers.league"});
    REQUIRE(byId(with) != byId(without));
    REQUIRE(byId(with).contains("spear"));
    REQUIRE(!byId(without).contains("spear"));
}

TEST_CASE("every shipped item appears under its own home axioms and under no unknown axiom")
{
    const DataTable* table = shippedCatalogue();
    REQUIRE(table != nullptr);
    const std::vector<CatalogueHome> homes = catalogueHomes(table);
    REQUIRE(homes.size() == 60);
    for (const CatalogueHome& entry : homes) {
        INFO(entry.item);
        REQUIRE(!entry.homes.empty());
        const std::set<std::string> own(entry.homes.begin(), entry.homes.end());
        REQUIRE(byId(availableItems(homes, own)).contains(entry.item));
        REQUIRE(!byId(availableItems(homes, {"no.such_axiom"})).contains(entry.item));
    }
}

TEST_CASE("one axiom gives a strict subset of the shipped catalogue")
{
    const std::vector<CatalogueHome> homes = catalogueHomes(shippedCatalogue());
    const auto one = availableItems(homes, {homes[0].homes[0]});
    const auto all = availableItems(homes, everyHome(homes));
    REQUIRE(all.size() == 60);
    REQUIRE(!one.empty());
    REQUIRE(one.size() < all.size());
}

TEST_CASE("a missing table gives an empty catalogue")
{
    REQUIRE(catalogueHomes(nullptr).empty());
}

TEST_CASE("the world subset is deterministic for a seed and differs across seeds")
{
    const std::vector<CatalogueHome> homes = catalogueHomes(shippedCatalogue());
    const auto candidates = availableItems(homes, everyHome(homes));

    const auto one = chooseWorldSubset(candidates, 7, 10);
    REQUIRE(one.size() == 10);
    REQUIRE(byId(one) == byId(chooseWorldSubset(candidates, 7, 10)));

    bool differs = false;
    for (std::uint64_t seed = 8; seed < 40 && !differs; ++seed) differs = byId(chooseWorldSubset(candidates, seed, 10)) != byId(one);
    REQUIRE(differs);
}

TEST_CASE("the world subset only holds available items, without repeats, capped at the candidates")
{
    const auto candidates = availableItems(kToy, {"law.hecatomb"});
    REQUIRE(chooseWorldSubset(candidates, 1, 99).size() == 2);
    REQUIRE(chooseWorldSubset(candidates, 1, 0).empty());
    REQUIRE(chooseWorldSubset({}, 1, 5).empty());
    for (const DistributedItem& item : chooseWorldSubset(candidates, 3, 1)) REQUIRE(byId(candidates).contains(item.item));
}

TEST_CASE("heavier items are drawn more often than lighter ones")
{
    const std::vector<DistributedItem> candidates = {{"heavy", 9}, {"light", 1}};
    int heavy = 0;
    for (std::uint64_t seed = 0; seed < 400; ++seed) {
        if (chooseWorldSubset(candidates, seed, 1)[0].item == "heavy") ++heavy;
    }
    REQUIRE(heavy > 300);
}

TEST_CASE("the world subset ignores zero weights and empty ids")
{
    const std::vector<DistributedItem> candidates = {{"zero", 0}, {"", 5}, {"ok", 2}};
    const auto got = chooseWorldSubset(candidates, 1, 5);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].item == "ok");
}
