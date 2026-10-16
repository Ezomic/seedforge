#include "ichor/data/data_set.h"
#include "ichor/data/loot_ratios.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ichor;

namespace {

DataTable table(int consumable, int material, int information, int coin, int gear, const std::string& tier = "notable")
{
    DataTable t;
    t.name = "catalogue";
    int n = 0;
    const auto add = [&](const char* cls, int count) {
        for (int i = 0; i < count; ++i) {
            DataRecord r;
            r.id = std::string(cls) + "_" + std::to_string(n++);
            r.fields["class"] = std::string(cls);
            r.fields["tier"] = tier;
            t.records[r.id] = r;
        }
    };
    add("consumable", consumable);
    add("material", material);
    add("information", information);
    add("coin", coin);
    add("gear", gear);
    return t;
}

const LootClassShare& share(const LootRatioReport& report, const std::string& name)
{
    for (const LootClassShare& s : report.classes) {
        if (s.name == name) return s;
    }
    FAIL("no such class " << name);
    return report.classes.front();
}

}

TEST_CASE("a catalogue at the doc 06 mix passes")
{
    const LootRatioReport report = checkLootRatios(table(55, 20, 10, 10, 5));
    INFO(report.text());
    REQUIRE(report.ok());
    REQUIRE(report.total == 100);
}

TEST_CASE("a mix within tolerance of the targets still passes")
{
    REQUIRE(checkLootRatios(table(52, 22, 9, 11, 6)).ok());
}

TEST_CASE("a gear-heavy catalogue goes red and names gear")
{
    const LootRatioReport report = checkLootRatios(table(20, 10, 10, 10, 50));
    REQUIRE(!report.ok());
    REQUIRE(!share(report, "gear").within);
    REQUIRE(share(report, "gear").deviation > 0.4);
    REQUIRE(!share(report, "consumable").within);
    REQUIRE(report.text().find("gear") != std::string::npos);
    REQUIRE(report.text().find("OUT") != std::string::npos);
}

TEST_CASE("a catalogue missing a whole class goes red")
{
    const LootRatioReport report = checkLootRatios(table(55, 20, 10, 0, 15));
    REQUIRE(!report.ok());
    REQUIRE(!share(report, "coin").within);
    REQUIRE(share(report, "coin").count == 0);
}

TEST_CASE("the tolerance is tunable")
{
    LootRatioTargets strict;
    strict.tolerance = 0.005;
    REQUIRE(!checkLootRatios(table(52, 22, 9, 11, 6), strict).ok());
}

TEST_CASE("unknown classes and tiers, and an empty catalogue, are reported")
{
    DataTable bad = table(55, 20, 10, 10, 5);
    bad.records["weird"].id = "weird";
    bad.records["weird"].fields["class"] = std::string("trinket");
    bad.records["weird"].fields["tier"] = std::string("legendary");
    const LootRatioReport report = checkLootRatios(bad);
    REQUIRE(!report.ok());
    REQUIRE(report.problems.size() == 2);

    REQUIRE(!checkLootRatios(DataTable{}).ok());
}

TEST_CASE("tiers are counted")
{
    const LootRatioReport report = checkLootRatios(table(55, 20, 10, 10, 5, "unique"));
    REQUIRE(report.tierCounts.at("unique") == 100);
}

TEST_CASE("the shipped catalogue is reported against doc 06")
{
    DataSet set;
    REQUIRE(set.loadDirectory(ICHOR_REPO_DATA));
    const DataTable* catalogue = set.table("catalogue");
    REQUIRE(catalogue != nullptr);
    const LootRatioReport report = checkLootRatios(*catalogue);
    INFO(report.text());
    REQUIRE(report.total == 60);
    REQUIRE(report.problems.empty());
    REQUIRE(report.tierCounts.at("unique") == 60);
}
