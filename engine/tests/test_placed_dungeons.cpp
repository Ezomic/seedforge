#include "ichor/graph/dungeon_validate_layout.h"
#include "ichor/graph/mission.h"
#include "ichor/world/placed_dungeons.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <string>

using namespace ichor;

namespace {

const DungeonSkeletonCatalog& shipped()
{
    static const DungeonSkeletonCatalog catalog = DungeonSkeletonCatalog::loadDirectory(std::filesystem::path(ICHOR_REPO_DATA) / "dungeon_skeletons");
    return catalog;
}

worldforge::ContentManifest manifestOf(std::uint64_t worldSeed, const std::vector<std::pair<std::string, std::string>>& dungeons)
{
    worldforge::ContentManifest m;
    m.worldSeed = worldSeed;
    for (const auto& [poi, archetype] : dungeons) {
        m.poiIds.push_back(poi);
        m.dungeons.push_back({poi, archetype, worldforge::dungeonSeed(worldSeed, poi)});
    }
    std::sort(m.poiIds.begin(), m.poiIds.end());
    std::sort(m.dungeons.begin(), m.dungeons.end(), [](const auto& a, const auto& b) { return a.poiId < b.poiId; });
    return m;
}

}

TEST_CASE("the shipped skeletons all load into the catalogue", "[placed_dungeons]")
{
    for (const std::string& e : shipped().errors()) {
        INFO(e);
        CHECK(e.empty());
    }
    CHECK(shipped().all().size() >= 30);
    CHECK_FALSE(shipped().forTheme("crypt").empty());
    CHECK(shipped().forTheme("no_such_theme").empty());
}

TEST_CASE("a slot bakes to the skeleton of its archetype", "[placed_dungeons]")
{
    const worldforge::ContentManifest m = manifestOf(7, {{"poi_0001", "crypt"}, {"poi_0002", "mine"}});
    DungeonLayouts layouts(m, shipped());
    CHECK(layouts.slotErrors().empty());
    CHECK(layouts.bakedCount() == 0);

    const DungeonLayoutResult crypt = layouts.layout("poi_0001");
    REQUIRE(crypt.ok());
    CHECK(crypt.dungeon->skeletonId == "sunken_crypt");
    CHECK(crypt.dungeon->seed == worldforge::dungeonSeed(7, "poi_0001"));
    CHECK(crypt.dungeon->graph.nodes().size() == crypt.dungeon->cells.size());
    CHECK(crypt.dungeon->layoutHash != 0);
    CHECK(layouts.bakedCount() == 1);

    const DungeonLayoutResult mine = layouts.layout("poi_0002");
    REQUIRE(mine.ok());
    CHECK(mine.dungeon->skeletonId == "flooded_mine");
    CHECK(mine.dungeon->layoutHash != crypt.dungeon->layoutHash);
}

TEST_CASE("the same slot gives the same layout, cached, whatever order it is asked in", "[placed_dungeons]")
{
    const worldforge::ContentManifest m = manifestOf(11, {{"poi_0001", "crypt"}, {"poi_0002", "crypt"}, {"poi_0003", "tower"}});
    DungeonLayouts a(m, shipped());
    DungeonLayouts b(m, shipped());
    const DungeonLayoutResult a3 = a.layout("poi_0003");
    const DungeonLayoutResult a1 = a.layout("poi_0001");
    const DungeonLayoutResult b1 = b.layout("poi_0001");
    const DungeonLayoutResult b3 = b.layout("poi_0003");
    REQUIRE(a1.ok());
    REQUIRE(a3.ok());
    REQUIRE(b1.ok());
    REQUIRE(b3.ok());
    CHECK(a1.dungeon->layoutHash == b1.dungeon->layoutHash);
    CHECK(a3.dungeon->layoutHash == b3.dungeon->layoutHash);
    CHECK(a.layout("poi_0001").dungeon == a1.dungeon);
    CHECK(a.bakedCount() == 2);
}

TEST_CASE("every shipped skeleton bakes to a layout that validates", "[placed_dungeons]")
{
    std::vector<std::pair<std::string, std::string>> all;
    for (const DungeonSkeleton& s : shipped().all()) all.emplace_back("poi_" + s.id, s.theme);
    const worldforge::ContentManifest m = manifestOf(3, all);
    DungeonLayouts layouts(m, shipped());
    REQUIRE(layouts.poiIds().size() == all.size());
    for (const std::string& id : layouts.poiIds()) {
        const DungeonLayoutResult r = layouts.layout(id);
        INFO(r.error);
        REQUIRE(r.ok());
        CHECK(graph::dungeon::validateLayout(graph::dungeon::fromGraph(r.dungeon->graph)).empty());
        CHECK(graph::mission::validateMission(r.dungeon->graph).empty());
    }
}

TEST_CASE("a slot with an unknown archetype is reported", "[placed_dungeons]")
{
    const worldforge::ContentManifest m = manifestOf(5, {{"poi_0001", "crypt"}, {"poi_0002", "cave"}});
    DungeonLayouts layouts(m, shipped());
    const std::vector<std::string> unknown = layouts.unknownArchetypes();
    REQUIRE(unknown.size() == 1);
    CHECK(unknown.front().find("poi_0002") != std::string::npos);
    CHECK(unknown.front().find("cave") != std::string::npos);

    const DungeonLayoutResult r = layouts.layout("poi_0002");
    CHECK_FALSE(r.ok());
    CHECK(r.dungeon == nullptr);
    CHECK(layouts.layout("poi_0002").error == r.error);
    CHECK(layouts.layout("poi_0001").ok());
}

TEST_CASE("hostile slots are rejected without crashing", "[placed_dungeons]")
{
    worldforge::ContentManifest m = manifestOf(9, {{"poi_0001", "crypt"}});
    m.dungeons.push_back({"poi_0001", "crypt", worldforge::dungeonSeed(9, "poi_0001")});
    m.dungeons.push_back({"poi_0002", "crypt", 12345});
    m.dungeons.push_back({"poi_0003", std::string(10000, 'x'), worldforge::dungeonSeed(9, "poi_0003")});
    DungeonLayouts layouts(m, shipped());
    CHECK(layouts.slotErrors().size() == 2);
    CHECK_FALSE(layouts.layout("poi_0002").ok());
    CHECK_FALSE(layouts.layout("").ok());
    CHECK_FALSE(layouts.layout("poi_0003").ok());
    CHECK(layouts.layout("poi_0001").ok());

    const worldforge::ContentManifest empty;
    DungeonLayouts none(empty, shipped());
    CHECK(none.poiIds().empty());
    CHECK_FALSE(none.layout("poi_0001").ok());
}

TEST_CASE("hostile skeleton text is skipped and named", "[placed_dungeons]")
{
    for (const char* text : {"", "not json", "{}", "[1,2]", "[{\"id\":\"a\"}]", "[{\"id\":\"a\",\"theme\":\"t\",\"rooms\":[\"x\"],\"links\":[]}]",
                             "[{\"id\":\"a\",\"theme\":\"t\",\"rooms\":[\"1:entry@0,0,0\"],\"links\":[\"1>9:path\"]}]",
                             "[{\"id\":\"a\",\"theme\":\"t\",\"rooms\":[\"1:entry@99999999999,0,0\"],\"links\":[]}]",
                             "[{\"id\":\"a\",\"theme\":\"t\",\"rooms\":[\"1:entry@0,0,0\"],\"links\":[]}]"}) {
        const DungeonSkeletonCatalog c = DungeonSkeletonCatalog::loadText("bad.json", text);
        INFO(text);
        CHECK(c.all().empty());
        CHECK_FALSE(c.errors().empty());
    }
    const DungeonSkeletonCatalog missing = DungeonSkeletonCatalog::loadDirectory("no/such/dir");
    CHECK(missing.all().empty());
    CHECK_FALSE(missing.errors().empty());
}

TEST_CASE("a theme with several skeletons is picked by seed, stably", "[placed_dungeons]")
{
    std::ifstream stream(std::filesystem::path(ICHOR_REPO_DATA) / "dungeon_skeletons" / "regional_tranche_1.json", std::ios::binary);
    nlohmann::json records = nlohmann::json::parse(stream);
    nlohmann::json twin = records.at(0);
    twin["id"] = "twin_of_first";
    records.push_back(twin);
    const DungeonSkeletonCatalog c = DungeonSkeletonCatalog::loadText("twin.json", records.dump());
    REQUIRE(c.errors().empty());
    const std::string theme = records.at(0)["theme"].get<std::string>();
    REQUIRE(c.forTheme(theme).size() == 2);

    std::set<std::string> picked;
    for (std::uint64_t w = 0; w < 64; ++w) {
        const worldforge::ContentManifest m = manifestOf(w, {{"poi_0001", theme}});
        DungeonLayouts layouts(m, c);
        DungeonLayouts again(m, c);
        const DungeonLayoutResult r = layouts.layout("poi_0001");
        REQUIRE(r.ok());
        CHECK(again.layout("poi_0001").dungeon->skeletonId == r.dungeon->skeletonId);
        picked.insert(r.dungeon->skeletonId);
    }
    CHECK(picked.size() == 2);
}

TEST_CASE("a duplicate skeleton id is reported and the later one ignored", "[placed_dungeons]")
{
    std::ifstream stream(std::filesystem::path(ICHOR_REPO_DATA) / "dungeon_skeletons" / "regional_tranche_1.json", std::ios::binary);
    nlohmann::json records = nlohmann::json::parse(stream);
    records.push_back(records.at(0));
    const DungeonSkeletonCatalog c = DungeonSkeletonCatalog::loadText("dup.json", records.dump());
    CHECK(c.all().size() == records.size() - 1);
    CHECK_FALSE(c.errors().empty());
}
