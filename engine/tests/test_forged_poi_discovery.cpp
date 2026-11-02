#include "forged_world_fixture.h"

#include "ichor/data/data_set.h"
#include "ichor/save/campaign.h"
#include "ichor/save/save_slots.h"
#include "ichor/ui/character_screen_model.h"
#include "ichor/world/forge_job.h"
#include "ichor/world/forged_campaign_hook.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/poi_discovery.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr int kGrid = 128;

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

std::unique_ptr<ForgedGame> makeGame()
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    const ForgedContentSetup setup = loadForgedContentSetup(data, ICHOR_GAME_DATA_DIR);
    const ForgeJobResult forged = test::forgedWorldFixture(descFor(1), kGrid, test::variantOfContentLists(setup.config));
    std::unique_ptr<ForgedGame> game = buildForgedGame(forged, descFor(1), setup.inputs);
    INFO(game->error);
    REQUIRE(game->ok());
    return game;
}

const ForgedGame& sharedGame()
{
    static const std::unique_ptr<ForgedGame> game = makeGame();
    return *game;
}

const PlacedPoi& firstOf(const ForgedGame& game, PlacedPoiKind kind)
{
    for (const PlacedPoi& poi : game.pois->all()) {
        if (poi.kind == kind) return poi;
    }
    FAIL("the world has no such place");
    return game.pois->all().front();
}

void movePlayer(Scene& scene, double x, double z)
{
    const entt::entity player = scene.find(StableId::of("player"));
    if (player == entt::null) {
        SpawnCommand spawn;
        spawn.id = StableId::of("player");
        spawn.name.value = "player";
        spawn.transform.position = {x, 0.0, z};
        spawn.player = Player{};
        scene.enqueue(spawn);
    } else {
        Transform moved = scene.registry().get<Transform>(player);
        moved.position = {x, 0.0, z};
        scene.enqueue(SetTransformCommand{StableId::of("player"), moved});
    }
    for (int i = 0; i < 3; ++i) scene.tick(kDt);
}

std::int64_t expectedXp(const PlacedPoi& poi, std::size_t prior)
{
    const DiscoveryKind kind = poi.kind == PlacedPoiKind::Dungeon ? DiscoveryKind::Poi : DiscoveryKind::Landmark;
    const double score = poi.kind == PlacedPoiKind::Settlement ? poi_discovery::kSettlementScore : poi.sightlinePermille / 1000.0;
    return discoveryXp(kind, score, prior);
}

}

TEST_CASE("entering a place grants its discovery xp once", "[poi_discovery]")
{
    const ForgedGame& game = sharedGame();
    const PlacedPoi& dungeon = firstOf(game, PlacedPoiKind::Dungeon);
    Scene scene(7);
    PoiDiscovery discovery(game);
    discovery.install(scene);

    movePlayer(scene, dungeon.x - 5000.0, dungeon.z);
    CHECK(discovery.found(scene).empty());
    CHECK(discovery.totalXp(scene) == 0);

    movePlayer(scene, dungeon.x, dungeon.z);
    REQUIRE(discovery.found(scene).size() == 1);
    CHECK(discovery.found(scene).front().poiId == dungeon.id);
    CHECK(discovery.found(scene).front().xp == expectedXp(dungeon, 0));
    CHECK(discovery.totalXp(scene) == expectedXp(dungeon, 0));
    CHECK_FALSE(discovery.notice(scene).empty());

    movePlayer(scene, dungeon.x - 5000.0, dungeon.z);
    movePlayer(scene, dungeon.x, dungeon.z);
    CHECK(discovery.found(scene).size() == 1);
    CHECK(discovery.totalXp(scene) == expectedXp(dungeon, 0));
}

TEST_CASE("a settlement and a landmark each pay once and repeats of a kind pay less", "[poi_discovery]")
{
    const ForgedGame& game = sharedGame();
    const PlacedPoi& settlement = firstOf(game, PlacedPoiKind::Settlement);
    const PlacedPoi& landmark = firstOf(game, PlacedPoiKind::Landmark);
    Scene scene(7);
    PoiDiscovery discovery(game);
    discovery.install(scene);

    movePlayer(scene, settlement.x, settlement.z);
    movePlayer(scene, landmark.x, landmark.z);
    const auto found = discovery.found(scene);
    REQUIRE(found.size() == 2);
    CHECK(found[0].xp == expectedXp(settlement, 0));
    CHECK(found[1].xp == expectedXp(landmark, 1));
    CHECK(found[0].xp > 0);
}

TEST_CASE("the found places survive a save and load and are not paid again", "[poi_discovery][campaign]")
{
    const ForgedGame& game = sharedGame();
    const PlacedPoi& dungeon = firstOf(game, PlacedPoiKind::Dungeon);
    const std::filesystem::path root = test::tempPath("ichor_poi_discovery_campaign");
    std::filesystem::remove_all(root);
    REQUIRE(saveCampaignWorld(root, "alpha", descFor(1), kGrid, game) == CampaignStatus::Ok);
    const auto campaign = Campaign::open(root, "alpha");
    REQUIRE(campaign.has_value());
    SaveSlots slots = campaign->slots();

    Scene scene(7);
    PoiDiscovery discovery(game);
    discovery.install(scene);
    movePlayer(scene, dungeon.x, dungeon.z);
    REQUIRE(discovery.found(scene).size() == 1);
    REQUIRE(slots.save("quick", scene, campaign->slotContext(5)));

    Scene loaded(7);
    PoiDiscovery again(game);
    again.install(loaded);
    REQUIRE(slots.load("quick", loaded, campaign->slotContext(5)).status == LoadStatus::Loaded);
    REQUIRE(again.found(loaded).size() == 1);
    CHECK(again.found(loaded).front().poiId == dungeon.id);
    CHECK(again.totalXp(loaded) == expectedXp(dungeon, 0));

    for (int i = 0; i < 5; ++i) loaded.tick(kDt);
    CHECK(again.found(loaded).size() == 1);
    CHECK(again.totalXp(loaded) == expectedXp(dungeon, 0));
    std::filesystem::remove_all(root);
}

TEST_CASE("the character screen lists discovery xp, newest first, under a total", "[poi_discovery][character_screen]")
{
    CharacterScreenInput input;
    input.discoveries = {{"settlement_0001", 50}, {"poi_0002", 30}};
    const auto sections = buildCharacterScreen(input);
    REQUIRE(sections.size() == 1);
    CHECK(sections[0].kind == CharacterSectionKind::Discoveries);
    REQUIRE(sections[0].lines.size() == 3);
    CHECK(sections[0].lines[0].text == "Discovery XP");
    CHECK(sections[0].lines[0].value == 80);
    CHECK(sections[0].lines[1].text == "poi_0002");
    CHECK(sections[0].lines[2].value == 50);

    CHECK(buildCharacterScreen(CharacterScreenInput{}).empty());
}
