#include "ichor/data/data_set.h"
#include "ichor/progression/capability_loot_access.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

using namespace ichor;

namespace {

using Strings = std::vector<std::string>;

std::vector<Capability> capTable()
{
    return {
        {"swim", "Swim", "", {}},
        {"dive", "Dive", "", {"swim"}},
        {"climb", "Climb", "", {}},
        {"descend", "Descend", "", {"climb"}},
        {"boat", "Boat", "", {}},
    };
}

std::vector<GearGrant> grantTable()
{
    return {
        {"diver_bell_helm", {"dive"}},
        {"cork_cuirass", {"swim"}},
        {"spurs", {"climb"}},
        {"rope", {"descend"}},
        {"punt_oar", {"boat"}},
        {"pearl_lungs", {"swim", "dive"}},
    };
}

std::vector<MapRegion> regions()
{
    return {
        {"market", {}},
        {"drowned_quarter", {"dive"}},
        {"sea_cliffs", {"climb"}},
        {"sea_cave", {"climb", "dive"}},
        {"far_island", {"boat"}},
    };
}

bool mentions(const Strings& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

Strings reach(const Strings& baseline, const Strings& items)
{
    return reachableRegionIds(regions(), effectiveCapabilities(grantTable(), capTable(), baseline, items));
}

}

TEST_CASE("with no gear only the open parts of the map are reachable", "[capability_loot]")
{
    CHECK(reach({}, {}) == Strings{"market"});
}

TEST_CASE("each piece of gear opens exactly the region it is for", "[capability_loot]")
{
    CHECK(reach({}, {"spurs"}) == Strings{"market", "sea_cliffs"});
    CHECK(reach({}, {"punt_oar"}) == Strings{"far_island", "market"});
    CHECK(reach({}, {"pearl_lungs"}) == Strings{"drowned_quarter", "market"});
}

TEST_CASE("a region needing two capabilities stays shut until both are held", "[capability_loot]")
{
    CHECK(reach({}, {"spurs"}) == Strings{"market", "sea_cliffs"});
    CHECK(reach({}, {"pearl_lungs", "spurs"}) == Strings{"drowned_quarter", "market", "sea_cave", "sea_cliffs"});

    const auto held = effectiveCapabilities(grantTable(), capTable(), {}, {"spurs"});
    const auto access = assessRegions(regions(), held);
    REQUIRE(access.size() == 5);
    CHECK(access[3].id == "sea_cave");
    CHECK_FALSE(access[3].reachable);
    CHECK(access[3].missing == Strings{"dive"});
}

TEST_CASE("redundant gear changes nothing", "[capability_loot]")
{
    CHECK(reach({}, {"spurs", "spurs", "spurs"}) == reach({}, {"spurs"}));
    CHECK(reach({}, {"diver_bell_helm", "cork_cuirass", "pearl_lungs"}) == reach({}, {"pearl_lungs"}));
    CHECK(effectiveCapabilities(grantTable(), capTable(), {}, {"cork_cuirass", "pearl_lungs"}) == Strings{"dive", "swim"});
}

TEST_CASE("item order does not change the answer", "[capability_loot]")
{
    CHECK(reach({}, {"spurs", "cork_cuirass", "diver_bell_helm"}) == reach({}, {"diver_bell_helm", "spurs", "cork_cuirass"}));
}

TEST_CASE("a capability whose prerequisite is missing is not held", "[capability_loot]")
{
    CHECK(effectiveCapabilities(grantTable(), capTable(), {}, {"diver_bell_helm"}).empty());
    CHECK(effectiveCapabilities(grantTable(), capTable(), {}, {"rope"}).empty());
    CHECK(reach({}, {"diver_bell_helm"}) == Strings{"market"});
    CHECK(reach({}, {"diver_bell_helm", "cork_cuirass"}) == Strings{"drowned_quarter", "market"});
}

TEST_CASE("an already unlocked capability supplies the prerequisite", "[capability_loot]")
{
    CHECK(effectiveCapabilities(grantTable(), capTable(), {"swim"}, {"diver_bell_helm"}) == Strings{"dive", "swim"});
    CHECK(effectiveCapabilities(grantTable(), capTable(), {"dive"}, {}).empty());
}

TEST_CASE("unknown items and hostile ids are ignored", "[capability_loot]")
{
    const std::string huge(100000, 'x');
    const Strings hostile = {"", "no_such_item", "SPURS", "spurs ", huge, std::string("spurs\0x", 7), "../../etc", "\xFF\xFE"};
    CHECK(effectiveCapabilities(grantTable(), capTable(), {}, hostile).empty());
    CHECK(effectiveCapabilities(grantTable(), capTable(), {huge, "", "fly"}, {}).empty());
    CHECK(reach({}, hostile) == Strings{"market"});
    CHECK(reach({}, {"no_such_item", "spurs"}) == Strings{"market", "sea_cliffs"});
}

TEST_CASE("a flood of carried items reads only a bounded number of them", "[capability_loot]")
{
    Strings items(100000, "no_such_item");
    items.push_back("spurs");
    CHECK(effectiveCapabilities(grantTable(), capTable(), {}, items).empty());

    Strings few(kMaxLootListed - 1, "no_such_item");
    few.push_back("spurs");
    CHECK(effectiveCapabilities(grantTable(), capTable(), {}, few) == Strings{"climb"});
}

TEST_CASE("a grant naming a capability the table does not know gives nothing", "[capability_loot]")
{
    const std::vector<GearGrant> grants = {{"wings", {"fly"}}, {"spurs", {"climb"}}};
    CHECK(effectiveCapabilities(grants, capTable(), {}, {"wings"}).empty());
    CHECK(effectiveCapabilities(grants, capTable(), {}, {"wings", "spurs"}) == Strings{"climb"});
}

TEST_CASE("a region needing an unknown capability can never be entered", "[capability_loot]")
{
    const std::vector<MapRegion> map = {{"sky", {"fly"}}};
    const auto access = assessRegions(map, {"climb", "dive", "swim", "boat", "descend"});
    REQUIRE(access.size() == 1);
    CHECK_FALSE(access[0].reachable);
    CHECK(access[0].missing == Strings{"fly"});
    CHECK(reachableRegionIds(map, {}).empty());
}

TEST_CASE("empty tables give empty answers", "[capability_loot]")
{
    CHECK(effectiveCapabilities({}, {}, {}, {"spurs"}).empty());
    CHECK(assessRegions({}, {"climb"}).empty());
    CHECK(reachableRegionIds({}, {}).empty());
}

TEST_CASE("repeated region needs list a missing capability once", "[capability_loot]")
{
    const std::vector<MapRegion> map = {{"x", {"dive", "dive", "climb"}}};
    CHECK(assessRegions(map, {}).front().missing == Strings{"climb", "dive"});
}

TEST_CASE("a grant table passes validation, and every kind of bad row is named", "[capability_loot]")
{
    Strings problems;
    CHECK(validateGearGrants(grantTable(), capTable(), problems));
    CHECK(problems.empty());

    problems.clear();
    CHECK_FALSE(validateGearGrants({{"a", {"fly"}}}, capTable(), problems));
    CHECK(mentions(problems, "[0].grants[0]"));

    problems.clear();
    CHECK_FALSE(validateGearGrants({{"a", {"climb"}}, {"a", {"boat"}}}, capTable(), problems));
    CHECK(mentions(problems, "[1].item"));

    problems.clear();
    CHECK_FALSE(validateGearGrants({{"a", {}}}, capTable(), problems));
    CHECK(mentions(problems, "[0].grants"));

    problems.clear();
    CHECK_FALSE(validateGearGrants({{"a", {"climb", "climb"}}}, capTable(), problems));
    CHECK(mentions(problems, "[0].grants[1]"));

    problems.clear();
    CHECK_FALSE(validateGearGrants({{"Bad Id", {"climb"}}, {"", {"climb"}}, {std::string(65, 'a'), {"climb"}}}, capTable(), problems));
    CHECK(mentions(problems, "[0].item"));
    CHECK(mentions(problems, "[1].item"));
    CHECK(mentions(problems, "[2].item"));
}

TEST_CASE("an oversized grant table is rejected without walking all of it", "[capability_loot]")
{
    std::vector<GearGrant> grants(kMaxLootGrants + 1, GearGrant{"a", {"climb"}});
    Strings problems;
    CHECK_FALSE(validateGearGrants(grants, capTable(), problems));
    CHECK(mentions(problems, "more than"));
    CHECK(problems.size() < 10);
}

TEST_CASE("a region table is validated", "[capability_loot]")
{
    Strings problems;
    CHECK(validateMapRegions(regions(), capTable(), problems));

    problems.clear();
    CHECK_FALSE(validateMapRegions({{"a", {"fly"}}, {"a", {}}, {"B!", {}}}, capTable(), problems));
    CHECK(mentions(problems, "[0].needs[0]"));
    CHECK(mentions(problems, "[1].id"));
    CHECK(mentions(problems, "[2].id"));
}

TEST_CASE("the shipped gear that grants traversal opens the matching parts of the map", "[capability_loot][data]")
{
    DataSet set;
    REQUIRE(set.loadDirectory(ICHOR_REPO_DATA));

    std::ifstream in(std::string(ICHOR_REPO_DATA) + "/progression/capabilities.json", std::ios::binary);
    const nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
    REQUIRE(root.is_array());
    std::vector<Capability> table;
    for (const auto& entry : root) {
        Capability c{entry.value("id", std::string()), "", "", {}};
        if (entry.contains("requires")) c.prerequisites = entry["requires"].get<Strings>();
        table.push_back(c);
    }

    std::vector<GearGrant> grants;
    const DataTable* catalogue = set.table("catalogue");
    REQUIRE(catalogue != nullptr);
    for (const auto& [id, item] : catalogue->records) {
        const auto it = item.fields.find("grants");
        if (it == item.fields.end()) continue;
        const auto* list = std::get_if<Strings>(&it->second);
        if (list && !list->empty()) grants.push_back({id, *list});
    }
    REQUIRE_FALSE(grants.empty());

    Strings problems;
    CHECK(validateGearGrants(grants, table, problems));
    CHECK(problems.empty());

    const std::vector<MapRegion> map = {{"open_ground", {}}, {"the_deep", {"dive"}}, {"the_crags", {"climb"}}, {"the_isles", {"boat"}}};
    const auto none = reachableRegionIds(map, effectiveCapabilities(grants, table, {}, {}));
    CHECK(none == Strings{"open_ground"});
    const auto helm = reachableRegionIds(map, effectiveCapabilities(grants, table, {}, {"diver_bell_helm", "cork_cuirass"}));
    CHECK(helm == Strings{"open_ground", "the_deep"});
    const auto spurs = reachableRegionIds(map, effectiveCapabilities(grants, table, {}, {"shepherds_climbing_spurs"}));
    CHECK(spurs == Strings{"open_ground", "the_crags"});
}
