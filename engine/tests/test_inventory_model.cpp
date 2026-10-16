#include "ichor/data/data_set.h"
#include "ichor/economy/inventory_model.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>

using namespace ichor;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

void put(DataTable& table, const std::string& id, const std::string& itemClass, double weight)
{
    DataRecord record;
    record.id = id;
    record.fields["class"] = itemClass;
    record.fields["weight"] = weight;
    table.records[id] = record;
}

DataTable sampleTable()
{
    DataTable table;
    table.name = "catalogue";
    put(table, "lamp", "consumable", 1.3);
    put(table, "ore", "material", 2.0);
    put(table, "spear", "gear", 3.4);
    put(table, "scroll", "information", 0.0);
    put(table, "obol", "coin", 0.01);
    return table;
}

ItemCatalogue sampleCatalogue()
{
    std::string error;
    auto catalogue = ItemCatalogue::fromTable(sampleTable(), StackRules{}, error);
    REQUIRE(catalogue.has_value());
    return *catalogue;
}

}

TEST_CASE("weightToUnits is exact and rejects hostile values", "[inventory]")
{
    CHECK(weightToUnits(1.2) == 1200);
    CHECK(weightToUnits(0.0) == 0);
    CHECK(weightToUnits(0.001) == 1);
    CHECK(weightToUnits(1000.0) == kMaxItemWeightUnits);
    CHECK_FALSE(weightToUnits(1000.001).has_value());
    CHECK_FALSE(weightToUnits(-0.5).has_value());
    CHECK_FALSE(weightToUnits(kNaN).has_value());
    CHECK_FALSE(weightToUnits(kInf).has_value());
    CHECK_FALSE(weightToUnits(-kInf).has_value());
    CHECK_FALSE(weightToUnits(1e300).has_value());
}

TEST_CASE("the catalogue rejects bad weights, classes and missing fields", "[inventory]")
{
    std::string error;
    for (const double bad : {kNaN, kInf, -1.0, 1e30}) {
        DataTable table = sampleTable();
        put(table, "bad", "gear", bad);
        CHECK_FALSE(ItemCatalogue::fromTable(table, StackRules{}, error).has_value());
        CHECK(error.find("bad") != std::string::npos);
    }
    DataTable noClass = sampleTable();
    put(noClass, "odd", "relic", 1.0);
    CHECK_FALSE(ItemCatalogue::fromTable(noClass, StackRules{}, error).has_value());

    DataTable noWeight = sampleTable();
    noWeight.records["mute"].id = "mute";
    noWeight.records["mute"].fields["class"] = std::string("gear");
    CHECK_FALSE(ItemCatalogue::fromTable(noWeight, StackRules{}, error).has_value());

    StackRules zero;
    zero.gear = 0;
    CHECK_FALSE(ItemCatalogue::fromTable(sampleTable(), zero, error).has_value());
    StackRules huge;
    huge.material = kMaxStackLimit + 1;
    CHECK_FALSE(ItemCatalogue::fromTable(sampleTable(), huge, error).has_value());
}

TEST_CASE("the shipped catalogue loads with every item weighed", "[inventory]")
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_REPO_DATA));
    const DataTable* table = data.table("catalogue");
    REQUIRE(table != nullptr);
    std::string error;
    const auto catalogue = ItemCatalogue::fromTable(*table, StackRules{}, error);
    REQUIRE(catalogue.has_value());
    CHECK(catalogue->size() == table->records.size());
    REQUIRE(catalogue->find("ledger_knife") != nullptr);
    CHECK(catalogue->find("ledger_knife")->weightUnits == 1200);
}

TEST_CASE("encumbrance thresholds are exact at the boundaries", "[inventory]")
{
    const EncumbranceThresholds t;
    CHECK(encumbranceLevel(0, 1000, t) == EncumbranceLevel::Light);
    CHECK(encumbranceLevel(499, 1000, t) == EncumbranceLevel::Light);
    CHECK(encumbranceLevel(500, 1000, t) == EncumbranceLevel::Loaded);
    CHECK(encumbranceLevel(799, 1000, t) == EncumbranceLevel::Loaded);
    CHECK(encumbranceLevel(800, 1000, t) == EncumbranceLevel::Heavy);
    CHECK(encumbranceLevel(1000, 1000, t) == EncumbranceLevel::Heavy);
    CHECK(encumbranceLevel(1001, 1000, t) == EncumbranceLevel::Over);
}

TEST_CASE("encumbrance rounds the threshold up so it never fires early", "[inventory]")
{
    // 50% of 3 is 1.5, so one unit is still Light and two is Loaded.
    CHECK(encumbranceLevel(1, 3) == EncumbranceLevel::Light);
    CHECK(encumbranceLevel(2, 3) == EncumbranceLevel::Loaded);
    // 80% of 7 is 5.6.
    CHECK(encumbranceLevel(5, 7) == EncumbranceLevel::Loaded);
    CHECK(encumbranceLevel(6, 7) == EncumbranceLevel::Heavy);
}

TEST_CASE("encumbrance survives extreme capacities", "[inventory]")
{
    CHECK(encumbranceLevel(0, kMax) == EncumbranceLevel::Light);
    CHECK(encumbranceLevel(kMax / 2 - 1, kMax) == EncumbranceLevel::Light);
    CHECK(encumbranceLevel(kMax / 2 + 1, kMax) == EncumbranceLevel::Loaded);
    CHECK(encumbranceLevel(kMax, kMax) == EncumbranceLevel::Heavy);
    CHECK(encumbranceLevel(kMax, kMax - 1) == EncumbranceLevel::Over);
}

TEST_CASE("zero capacity is Over as soon as anything is carried", "[inventory]")
{
    CHECK(encumbranceLevel(0, 0) == EncumbranceLevel::Light);
    CHECK(encumbranceLevel(1, 0) == EncumbranceLevel::Over);
}

TEST_CASE("invalid thresholds fall back to the defaults", "[inventory]")
{
    const EncumbranceThresholds backwards{900, 100};
    const EncumbranceThresholds zero{0, 500};
    const EncumbranceThresholds past{500, 1001};
    for (const auto& bad : {backwards, zero, past}) {
        CHECK_FALSE(bad.valid());
        CHECK(encumbranceLevel(499, 1000, bad) == EncumbranceLevel::Light);
        CHECK(encumbranceLevel(500, 1000, bad) == EncumbranceLevel::Loaded);
        CHECK(encumbranceLevel(800, 1000, bad) == EncumbranceLevel::Heavy);
    }
}

TEST_CASE("custom thresholds move the boundaries", "[inventory]")
{
    const EncumbranceThresholds t{250, 1000};
    CHECK(encumbranceLevel(249, 1000, t) == EncumbranceLevel::Light);
    CHECK(encumbranceLevel(250, 1000, t) == EncumbranceLevel::Loaded);
    CHECK(encumbranceLevel(999, 1000, t) == EncumbranceLevel::Loaded);
    CHECK(encumbranceLevel(1000, 1000, t) == EncumbranceLevel::Heavy);
}

TEST_CASE("adding tracks weight and walks up the levels", "[inventory]")
{
    const ItemCatalogue catalogue = sampleCatalogue();
    Inventory bag(catalogue, 10'000);
    CHECK(bag.level() == EncumbranceLevel::Light);

    CHECK(bag.add("ore", 2).status == InventoryStatus::Ok);
    CHECK(bag.totalWeightUnits() == 4000);
    CHECK(bag.level() == EncumbranceLevel::Light);

    CHECK(bag.add("lamp", 1).status == InventoryStatus::Ok);
    CHECK(bag.level() == EncumbranceLevel::Loaded);

    CHECK(bag.add("spear", 1).status == InventoryStatus::Ok);
    CHECK(bag.level() == EncumbranceLevel::Heavy);
    CHECK(bag.count("ore") == 2);
}

TEST_CASE("add refuses past capacity but allows exactly capacity", "[inventory]")
{
    const ItemCatalogue catalogue = sampleCatalogue();
    Inventory bag(catalogue, 4000);
    CHECK(bag.add("ore", 2).status == InventoryStatus::Ok);
    CHECK(bag.totalWeightUnits() == 4000);
    CHECK(bag.level() == EncumbranceLevel::Heavy);

    const auto refused = bag.add("obol", 1);
    CHECK(refused.status == InventoryStatus::OverCapacity);
    CHECK(refused.changed == 0);
    CHECK(bag.count("obol") == 0);
    CHECK(bag.totalWeightUnits() == 4000);
}

TEST_CASE("addForced may go Over and the state is unmistakable", "[inventory]")
{
    const ItemCatalogue catalogue = sampleCatalogue();
    Inventory bag(catalogue, 3000);
    CHECK(bag.addForced("spear", 1).status == InventoryStatus::Ok);
    CHECK(bag.level() == EncumbranceLevel::Over);
    CHECK(bag.add("scroll", 1).status == InventoryStatus::OverCapacity);

    CHECK(bag.remove("spear", 1).status == InventoryStatus::Ok);
    CHECK(bag.level() == EncumbranceLevel::Light);
    CHECK(bag.totalWeightUnits() == 0);
}

TEST_CASE("shrinking capacity can push a full bag Over", "[inventory]")
{
    const ItemCatalogue catalogue = sampleCatalogue();
    Inventory bag(catalogue, 10'000);
    REQUIRE(bag.add("ore", 4).status == InventoryStatus::Ok);
    CHECK(bag.level() == EncumbranceLevel::Heavy);
    bag.setCapacity(7999);
    CHECK(bag.level() == EncumbranceLevel::Over);
    bag.setCapacity(8000);
    CHECK(bag.level() == EncumbranceLevel::Heavy);
}

TEST_CASE("unknown ids and zero counts change nothing", "[inventory]")
{
    const ItemCatalogue catalogue = sampleCatalogue();
    Inventory bag(catalogue, 10'000);
    CHECK(bag.add("nope", 1).status == InventoryStatus::UnknownItem);
    CHECK(bag.addForced("", 1).status == InventoryStatus::UnknownItem);
    CHECK(bag.add("ore", 0).status == InventoryStatus::ZeroCount);
    CHECK(bag.remove("ore", 0).status == InventoryStatus::ZeroCount);
    CHECK(bag.remove("nope", 1).status == InventoryStatus::NotHeld);
    CHECK(bag.totalWeightUnits() == 0);
    CHECK(bag.rows().empty());
}

TEST_CASE("stack limits hold and a refused add is all or nothing", "[inventory]")
{
    const ItemCatalogue catalogue = sampleCatalogue();
    Inventory bag(catalogue, 1'000'000);
    CHECK(bag.add("lamp", 5).status == InventoryStatus::Ok);
    CHECK(bag.add("lamp", 1).status == InventoryStatus::StackFull);
    CHECK(bag.count("lamp") == 5);

    CHECK(bag.add("spear", 1).status == InventoryStatus::Ok);
    CHECK(bag.addForced("spear", 1).status == InventoryStatus::StackFull);
    CHECK(bag.add("ore", 11).status == InventoryStatus::StackFull);
    CHECK(bag.count("ore") == 0);
    CHECK(bag.add("ore", std::numeric_limits<std::uint32_t>::max()).status == InventoryStatus::StackFull);
    CHECK(bag.totalWeightUnits() == 5 * 1300 + 3400);
}

TEST_CASE("remove is all or nothing and drops empty stacks", "[inventory]")
{
    const ItemCatalogue catalogue = sampleCatalogue();
    Inventory bag(catalogue, 100'000);
    REQUIRE(bag.add("ore", 3).status == InventoryStatus::Ok);
    CHECK(bag.remove("ore", 4).status == InventoryStatus::NotHeld);
    CHECK(bag.count("ore") == 3);
    CHECK(bag.remove("ore", 3).status == InventoryStatus::Ok);
    CHECK(bag.rows().empty());
}

TEST_CASE("a zero weight item is carried for free", "[inventory]")
{
    const ItemCatalogue catalogue = sampleCatalogue();
    Inventory bag(catalogue, 0);
    CHECK(bag.add("scroll", 1).status == InventoryStatus::Ok);
    CHECK(bag.level() == EncumbranceLevel::Light);
}

TEST_CASE("the heaviest allowed weights never overflow a total", "[inventory]")
{
    DataTable table;
    table.name = "catalogue";
    put(table, "anvil", "coin", 1000.0);
    StackRules rules;
    rules.coin = kMaxStackLimit;
    std::string error;
    const auto catalogue = ItemCatalogue::fromTable(table, rules, error);
    REQUIRE(catalogue.has_value());

    Inventory bag(*catalogue, kMax);
    CHECK(bag.addForced("anvil", kMaxStackLimit).status == InventoryStatus::Ok);
    CHECK(bag.totalWeightUnits() == std::uint64_t{kMaxStackLimit} * kMaxItemWeightUnits);
    CHECK(bag.level() == EncumbranceLevel::Light);
}

TEST_CASE("rows give a deterministic weight column and sorts", "[inventory]")
{
    const ItemCatalogue catalogue = sampleCatalogue();
    Inventory bag(catalogue, 100'000);
    REQUIRE(bag.add("spear", 1).status == InventoryStatus::Ok);
    REQUIRE(bag.add("ore", 3).status == InventoryStatus::Ok);
    REQUIRE(bag.add("lamp", 3).status == InventoryStatus::Ok);

    const auto byId = bag.rows();
    REQUIRE(byId.size() == 3);
    CHECK(byId[0].id == "lamp");
    CHECK(byId[0].totalWeightUnits == 3900);
    CHECK(byId[0].unitWeightUnits == 1300);

    const auto byWeight = bag.rows(InventorySort::ByWeightDescending);
    CHECK(byWeight[0].id == "ore");
    CHECK(byWeight[1].id == "lamp");
    CHECK(byWeight[2].id == "spear");

    const auto byCount = bag.rows(InventorySort::ByCountDescending);
    CHECK(byCount[0].id == "lamp"); // tie with ore at 3 broken by id
    CHECK(byCount[1].id == "ore");
    CHECK(byCount[2].id == "spear");
}
