#include "ichor/data/data_set.h"
#include "ichor/economy/loot_use.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

void put(DataTable& table, const std::string& id, const std::string& itemClass, double weight)
{
    DataRecord record;
    record.id = id;
    record.fields["class"] = itemClass;
    record.fields["weight"] = weight;
    table.records[id] = record;
}

ItemCatalogue catalogue()
{
    DataTable table;
    table.name = "catalogue";
    put(table, "flask", "consumable", 1.0);
    put(table, "ore", "material", 10.0);
    std::string error;
    auto c = ItemCatalogue::fromTable(table, StackRules{}, error);
    REQUIRE(c.has_value());
    return *c;
}

const ConsumableDef kFlask{"flask", 3, "heal", 20};

}

TEST_CASE("encumbrance slows and tires the bearer level by level", "[loot]")
{
    const auto light = encumbrancePenalty(EncumbranceLevel::Light);
    const auto loaded = encumbrancePenalty(EncumbranceLevel::Loaded);
    const auto heavy = encumbrancePenalty(EncumbranceLevel::Heavy);
    const auto over = encumbrancePenalty(EncumbranceLevel::Over);
    CHECK(light.movePermille == 1000);
    CHECK(light.canSprint);
    CHECK(loaded.movePermille < light.movePermille);
    CHECK(heavy.movePermille < loaded.movePermille);
    CHECK(over.movePermille < heavy.movePermille);
    CHECK(loaded.staminaCostPermille > light.staminaCostPermille);
    CHECK(over.staminaCostPermille > heavy.staminaCostPermille);
    CHECK_FALSE(heavy.canSprint);
}

TEST_CASE("a full pack moves slower than an empty one", "[loot]")
{
    const ItemCatalogue c = catalogue();
    Inventory pack(c, 100'000);
    const auto emptySpeed = scalePermille(5000, encumbrancePenalty(pack.level()).movePermille);
    REQUIRE(pack.add("ore", 9).status == InventoryStatus::Ok); // 90 of 100 units
    REQUIRE(pack.level() == EncumbranceLevel::Heavy);
    const auto loadedSpeed = scalePermille(5000, encumbrancePenalty(pack.level()).movePermille);
    CHECK(emptySpeed == 5000);
    CHECK(loadedSpeed == 3500);
}

TEST_CASE("scalePermille is exact and never overflows", "[loot]")
{
    constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
    CHECK(scalePermille(1999, 500) == 999);
    CHECK(scalePermille(0, 3000) == 0);
    CHECK(scalePermille(kMax, 1000) == kMax);
    CHECK(scalePermille(kMax, 3000) == kMax);
    CHECK(scalePermille(kMax, 0) == 0);
}

TEST_CASE("a consumable opens one item and spends charges one at a time", "[loot]")
{
    const ItemCatalogue c = catalogue();
    Inventory pack(c, 100'000);
    ChargeLedger ledger;
    REQUIRE(pack.add("flask", 2).status == InventoryStatus::Ok);

    auto r = useConsumable(pack, ledger, kFlask);
    CHECK(r.status == UseStatus::Ok);
    CHECK(r.effect == "heal");
    CHECK(r.magnitude == 20);
    CHECK(r.chargesLeft == 2);
    CHECK_FALSE(r.itemSpent);
    CHECK(pack.count("flask") == 1);
    CHECK(ledger.remaining("flask") == 2);

    useConsumable(pack, ledger, kFlask);
    r = useConsumable(pack, ledger, kFlask);
    CHECK(r.itemSpent);
    CHECK(r.chargesLeft == 0);
    CHECK(ledger.remaining("flask") == 0);
    CHECK(pack.count("flask") == 1);

    r = useConsumable(pack, ledger, kFlask);
    CHECK(r.status == UseStatus::Ok);
    CHECK(pack.count("flask") == 0);
}

TEST_CASE("using what is not held or a bad def changes nothing", "[loot]")
{
    const ItemCatalogue c = catalogue();
    Inventory pack(c, 100'000);
    ChargeLedger ledger;
    CHECK(useConsumable(pack, ledger, kFlask).status == UseStatus::NotHeld);
    CHECK(ledger.remaining("flask") == 0);
    REQUIRE(pack.add("flask", 1).status == InventoryStatus::Ok);
    CHECK(useConsumable(pack, ledger, {"flask", 0, "heal", 1}).status == UseStatus::InvalidDef);
    CHECK(useConsumable(pack, ledger, {"", 1, "heal", 1}).status == UseStatus::InvalidDef);
    CHECK(useConsumable(pack, ledger, {"flask", kMaxStackLimit + 1, "heal", 1}).status == UseStatus::InvalidDef);
    CHECK(pack.count("flask") == 1);
}

TEST_CASE("loot rolls are deterministic and follow the weights", "[loot]")
{
    const std::vector<LootEntry> table{{"flask", 9, 1, 2}, {"ore", 1, 1, 1}};
    Rng a(42);
    Rng b(42);
    const auto first = rollLoot(table, 50, a);
    const auto second = rollLoot(table, 50, b);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].itemId == second[i].itemId);
        CHECK(first[i].count == second[i].count);
    }
    std::uint32_t flasks = 0;
    std::uint32_t ore = 0;
    Rng c(7);
    for (const LootDrop& d : rollLoot(table, 1000, c)) {
        (d.itemId == "flask" ? flasks : ore) += d.count;
    }
    CHECK(flasks > ore * 5);
    CHECK(ore > 0);
}

TEST_CASE("hostile loot tables yield nothing", "[loot]")
{
    Rng rng(1);
    CHECK(rollLoot({}, 5, rng).empty());
    CHECK(rollLoot({{"x", 0, 1, 1}}, 5, rng).empty());
    CHECK(rollLoot({{"x", 1, 0, 1}}, 5, rng).empty());
    CHECK(rollLoot({{"x", 1, 3, 2}}, 5, rng).empty());
    CHECK(rollLoot({{"", 1, 1, 1}}, 5, rng).empty());
    CHECK(rollLoot({{"x", 1, 1, 1}}, 0, rng).empty());
    CHECK(rollLoot({{"x", std::numeric_limits<std::uint32_t>::max(), 1, 1}}, 1u << 30, rng).size() == 1);
    const auto big = rollLoot({{"x", 1, std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max()}}, 5, rng);
    REQUIRE(big.size() == 1);
    CHECK(big[0].count == std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("collecting loot leaves behind what the load refuses", "[loot]")
{
    const ItemCatalogue c = catalogue();
    Inventory pack(c, 25'000); // room for two ore and change
    const auto r = collectLoot(pack, {{"flask", 2}, {"ore", 5}, {"ore", 2}, {"nope", 1}});
    REQUIRE(r.taken.size() == 2);
    CHECK(r.taken[0].itemId == "flask");
    CHECK(r.taken[1].count == 2);
    REQUIRE(r.leftBehind.size() == 2);
    CHECK(r.leftBehind[0].count == 5);
    CHECK(r.leftBehind[1].itemId == "nope");
    CHECK(pack.totalWeightUnits() <= pack.capacityUnits());
}
