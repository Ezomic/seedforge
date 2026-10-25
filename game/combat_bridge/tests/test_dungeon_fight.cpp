#include "ichor/combat_bridge/dungeon_fight.h"
#include "ichor/data/data_set.h"
#include "ichor/save/save_slots.h"
#include "ichor/world/dungeon_content.h"
#include "ichor/world/forge_job.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>

using namespace ichor;

namespace {

constexpr int kGrid = 128;
constexpr double kDt = 1.0 / 60.0;

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

// A world, a dungeon in it and a room of that dungeon that holds two enemies, found by search: no seed is assumed to have one.
struct Target {
    ForgedContentSetup setup;
    std::unique_ptr<ForgedGame> game;
    const PlacedPoi* poi = nullptr;
    int roomIndex = -1;
    graph::NodeId room = 0;
    std::vector<DungeonEnemy> enemies; // the room's
    std::size_t roomsWithEnemies = 0;
};

bool tryForge(std::uint64_t seed, Target& out)
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    out.setup = loadForgedContentSetup(data, ICHOR_GAME_DATA_DIR);
    ForgeJob job(descFor(seed), kGrid, {}, out.setup.config);
    REQUIRE(job.start());
    for (int i = 0; i < 12000 && job.state() == ForgeJobState::Running; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO(job.error());
    REQUIRE(job.state() == ForgeJobState::Done);
    out.game = buildForgedGame(*job.result(), descFor(seed), out.setup.inputs);
    REQUIRE(out.game->ok());
    if (!out.game->pois || !out.game->dungeons || !findCombatSite(*out.game).found) return false;
    for (const PlacedPoi& poi : out.game->pois->all()) {
        if (poi.kind != PlacedPoiKind::Dungeon) continue;
        const DungeonLayoutResult layout = out.game->dungeons->layout(poi.id);
        if (!layout.ok()) continue;
        const DungeonGreyBox box = buildDungeonGreyBox(*layout.dungeon, glm::dvec3(dungeon_entry::kPocketX, 0.0, dungeon_entry::kPocketX));
        if (!box.ok()) continue;
        const DungeonContent content = buildDungeonContent(*layout.dungeon, box, {});
        for (std::size_t i = 0; i < content.rooms.size(); ++i) {
            const std::vector<std::size_t> inRoom = content.enemiesIn(content.rooms[i].id);
            if (inRoom.size() != 2) continue;
            out.poi = &poi;
            out.roomIndex = static_cast<int>(i);
            out.room = content.rooms[i].id;
            for (const std::size_t e : inRoom) out.enemies.push_back(content.enemies[e]);
            out.roomsWithEnemies = roomsWithEnemies(content);
            return true;
        }
    }
    return false;
}

Target& target()
{
    static Target found = [] {
        Target out;
        for (std::uint64_t seed = 1; seed <= 12; ++seed) {
            out = Target{};
            if (tryForge(seed, out)) return out;
        }
        FAIL("no seed under 13 forged a world with a dungeon room holding two enemies");
        return out;
    }();
    return found;
}

// The app's order of installing: bridge, then the dungeon entry, then the fight.
struct Rig {
    Scene scene{7};
    CombatSite site;
    std::unique_ptr<CombatBridge> bridge;
    std::unique_ptr<DungeonEntry> entry;
    std::unique_ptr<DungeonFight> fight;
    bool worldReady = false; // the ground at the enemy's world post; off, so the world enemy never spawns and the dungeon is where the fight starts

    explicit Rig(int startRoom, DungeonFightConfig config = {})
    {
        ForgedGame& game = *target().game;
        const PlacedPoi& poi = *target().poi;
        site = findCombatSite(game);
        Transform ground;
        ground.position = {poi.x, static_cast<double>(poi.groundY) - 1.0, poi.z};
        scene.physics().addStaticBox(StableId::of("test_ground"), StaticBox{{400.0f, 1.0f, 400.0f}}, ground);
        const glm::dvec3 start{poi.x - 4.0, static_cast<double>(poi.groundY) + 0.5, poi.z};
        SpawnCommand player;
        player.id = StableId::of("player");
        player.name.value = "player";
        player.transform.position = start;
        player.player = Player{};
        player.character = CharacterBody{};
        scene.enqueue(player);
        CombatBridgeConfig bridgeConfig;
        bridgeConfig.legacyControls = true;
        bridge = std::make_unique<CombatBridge>(game, site, bridgeConfig);
        bridge->setGroundCheck([this](glm::dvec3) { return worldReady; });
        bridge->install(scene, StableId::of("player"), start);
        entry = std::make_unique<DungeonEntry>(game, 0, DungeonEntryParams{.autoEnter = startRoom >= 0, .startRoom = startRoom});
        entry->install(scene);
        fight = std::make_unique<DungeonFight>(*bridge, *entry, config);
        fight->install(scene, StableId::of("player"));
    }

    // Plays the bot's input (nothing without a bot) until `done` or the ticks run out; the ticks it took, or -1.
    int play(int maxTicks, const std::function<bool()>& done)
    {
        for (int i = 0; i < maxTicks; ++i) {
            if (done()) return i;
            scene.tick(kDt, bridge->botInput(scene));
        }
        return done() ? maxTicks : -1;
    }
    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i) scene.tick(kDt, bridge->botInput(scene));
    }

    entt::entity player() { return scene.find(StableId::of("player")); }
    glm::dvec3 playerAt() { return scene.registry().get<Transform>(player()).position; }
    const stage0::Combatant& fighter() { return scene.registry().get<stage0::Combatant>(player()); }
    const stage0::Combatant& enemy() { return scene.registry().get<stage0::Combatant>(scene.find(bridge->stage().foeId())); }
    glm::dvec3 enemyAt() { return scene.registry().get<Transform>(scene.find(bridge->stage().foeId())).position; }
    bool killed(const DungeonEnemy& e) { return enemyKilled(scene, target().poi->id, e.room, e.slot); }
    bool roomDone() { return roomCleared(scene, entry->content(), target().room); }
    bool inRoom() { return entry->content().roomAt(playerAt()) != nullptr && entry->content().roomAt(playerAt())->id == target().room; }
};

struct Played {
    int ticks = -1;
    std::uint64_t hash = 0;
    float playerHealth = 0.0f;
    int respawns = 0;
};

// A bot of the given skill fights the room's enemies; ticks is -1 when they are not all dead in time.
Played fightRoom(const stage0::BotSkill& skill, std::uint64_t botSeed)
{
    Rig rig(target().roomIndex);
    rig.bridge->enableBot(skill, botSeed);
    Played played;
    played.ticks = rig.play(60 * 150, [&] { return rig.roomDone(); });
    played.hash = rig.scene.stateHash();
    played.playerHealth = rig.fighter().health;
    played.respawns = rig.bridge->respawns();
    return played;
}

// The first bot seed whose fight clears the room without a death; the search is part of the test, so a retuned fight finds a new one.
bool findClearingSeed(std::uint64_t& seed)
{
    static std::uint64_t found = 0;
    if (found == 0) {
        const stage0::BotSkill expert = *stage0::findBotSkill("expert");
        for (std::uint64_t candidate = 1; candidate <= 60 && found == 0; ++candidate) {
            const Played played = fightRoom(expert, candidate);
            if (played.ticks >= 0 && played.respawns == 0) found = candidate;
        }
    }
    seed = found;
    return found != 0;
}

}

TEST_CASE("stepping into a dungeon room with enemies wakes the first of them, and the rest wait their turn", "[dungeon_fight]")
{
    Rig rig(target().roomIndex);
    REQUIRE(rig.play(60 * 10, [&] { return rig.fight->engaged().has_value(); }) >= 0);
    REQUIRE(rig.fight->inDungeon());
    CHECK(rig.bridge->live());
    CHECK_FALSE(rig.bridge->foeParked());
    CHECK(rig.enemy().alive());
    CHECK(rig.inRoom());

    const DungeonEnemy& first = target().enemies.front();
    const DungeonEnemy& second = target().enemies.back();
    const std::size_t engaged = *rig.fight->engaged();
    CHECK(rig.entry->content().enemies[engaged].slot == first.slot);
    // The awake enemy stands at its own post, in its own room, on the floor of the hall.
    for (int i = 0; i < 20; ++i) rig.tick();
    const glm::dvec3 at = rig.enemyAt();
    CHECK(std::hypot(at.x - first.post.x, at.z - first.post.z) < 1.5);
    CHECK(at.y == Catch::Approx(first.post.y).margin(0.6));

    // Nobody has been killed, so no marker exists and the second still waits.
    CHECK_FALSE(rig.killed(first));
    CHECK_FALSE(rig.killed(second));
    RenderView view;
    rig.fight->appendView(rig.scene, view);
    CHECK(view.instances.empty()); // no art in the test: the waiting enemy is only drawn with the Bull-Warden loaded
}

TEST_CASE("an enemy the player walks away from goes back to its post unhurt, and the room is not cleared", "[dungeon_fight]")
{
    Rig rig(target().roomIndex);
    REQUIRE(rig.play(60 * 10, [&] { return rig.fight->engaged().has_value(); }) >= 0);
    rig.bridge->stage().receiveHit(rig.scene, rig.scene.find(rig.bridge->stage().foeId()), rig.player(), 50.0f, 0.0f, true);
    REQUIRE(rig.enemy().health < rig.bridge->stage().tuning().maxHealth);

    SetTransformCommand toEntry;
    toEntry.id = StableId::of("player");
    toEntry.transform.position = rig.entry->greyBox().spawn;
    rig.scene.enqueue(toEntry);
    rig.tick(4);
    CHECK_FALSE(rig.fight->engaged().has_value());
    CHECK(rig.bridge->foeParked());
    CHECK_FALSE(rig.enemy().alive());
    for (const DungeonEnemy& e : target().enemies) CHECK_FALSE(rig.killed(e));
    CHECK_FALSE(rig.roomDone());

    SetTransformCommand back;
    back.id = StableId::of("player");
    back.transform.position = rig.entry->content().room(target().room)->centre + glm::dvec3(0.0, 0.5, 0.0);
    rig.scene.enqueue(back);
    REQUIRE(rig.play(60 * 6, [&] { return rig.fight->engaged().has_value(); }) >= 0);
    CHECK(rig.enemy().health == rig.bridge->stage().tuning().maxHealth);
}

TEST_CASE("a scripted bot clears a dungeon room enemy by enemy, the same way twice, and each kill is a marker in the scene", "[dungeon_fight]")
{
    std::uint64_t botSeed = 0;
    REQUIRE(findClearingSeed(botSeed));
    const stage0::BotSkill expert = *stage0::findBotSkill("expert");
    const Played first = fightRoom(expert, botSeed);
    const Played second = fightRoom(expert, botSeed);
    CHECK(first.ticks > 60 * 3);
    CHECK(first.ticks == second.ticks);
    CHECK(first.hash == second.hash);
    CHECK(first.playerHealth > 0.0f);
    CHECK(first.respawns == 0);

    Rig rig(target().roomIndex);
    rig.bridge->enableBot(expert, botSeed);
    // The turns: the first dies while the second has not been woken, then the second takes its post.
    REQUIRE(rig.play(60 * 100, [&] { return rig.killed(target().enemies.front()); }) >= 0);
    CHECK_FALSE(rig.roomDone());
    CHECK_FALSE(rig.killed(target().enemies.back()));
    REQUIRE(rig.play(60 * 100, [&] { return rig.killed(target().enemies.back()); }) >= 0);
    CHECK(rig.roomDone());
    CHECK(clearedRooms(rig.scene, rig.entry->content()) == 1);
    CHECK(rig.fighter().health > 0.0f);

    // A cleared room wakes nobody: the player stands in it and nothing engages.
    rig.tick(60 * 5);
    CHECK(rig.inRoom());
    CHECK_FALSE(rig.fight->engaged().has_value());
}

TEST_CASE("the exit is open whatever is alive: with an enemy engaged, Confirm at the exit leaves, and the enemy returns to the world", "[dungeon_fight]")
{
    Rig rig(target().roomIndex);
    REQUIRE(rig.play(60 * 10, [&] { return rig.fight->engaged().has_value(); }) >= 0);
    REQUIRE(rig.enemy().alive());

    const DungeonGreyBox hall = rig.entry->greyBox();
    SetTransformCommand toExit;
    toExit.id = StableId::of("player");
    toExit.transform.position = {hall.exitPoint.x + 0.5, hall.exitPoint.y + 0.1, hall.exitPoint.z};
    rig.scene.enqueue(toExit);
    rig.tick(20);
    REQUIRE(rig.entry->prompt(rig.scene).find("leave") != std::string::npos);

    rig.scene.tick(kDt, InputFrame{.buttons = InputFrame::Confirm});
    REQUIRE(rig.play(60 * 5, [&] { return !rig.entry->inside(rig.scene); }) >= 0);
    rig.tick(10);
    CHECK_FALSE(rig.fight->inDungeon());
    CHECK_FALSE(inDungeonPocket(rig.playerAt()));
    for (const DungeonEnemy& e : target().enemies) CHECK_FALSE(rig.killed(e));

    // The enemy is back at its world post once that ground is there, and not before.
    rig.tick(30);
    CHECK(rig.bridge->foeParked());
    rig.worldReady = true;
    rig.tick(10);
    CHECK_FALSE(rig.bridge->foeParked());
    CHECK(rig.enemy().alive());
    CHECK(std::hypot(rig.enemyAt().x - rig.site.enemyPost.x, rig.enemyAt().z - rig.site.enemyPost.z) < 2.0);
}

TEST_CASE("a player who dies in a dungeon room stands at the dungeon's entry again, with the room's enemies unhurt and unkilled", "[dungeon_fight]")
{
    Rig rig(target().roomIndex);
    REQUIRE(rig.play(60 * 10, [&] { return rig.fight->engaged().has_value(); }) >= 0);
    rig.bridge->stage().receiveHit(rig.scene, rig.player(), rig.scene.find(rig.bridge->stage().foeId()), 500.0f, 0.0f, true);
    REQUIRE_FALSE(rig.fighter().alive());
    REQUIRE(rig.play(60 * 8, [&] { return rig.bridge->respawns() == 1; }) >= 0);
    rig.tick(10);
    CHECK(rig.fighter().alive());
    const DungeonRoom* entryRoom = rig.entry->content().roomAt(rig.playerAt());
    REQUIRE(entryRoom != nullptr);
    CHECK(entryRoom->type == "entry");
    CHECK(inDungeonPocket(rig.playerAt()));
    CHECK_FALSE(rig.fight->engaged().has_value());
    for (const DungeonEnemy& e : target().enemies) CHECK_FALSE(rig.killed(e));
}

TEST_CASE("a cleared room stays cleared through a save and a load, and an uncleared room does not", "[dungeon_fight][save]")
{
    std::uint64_t botSeed = 0;
    REQUIRE(findClearingSeed(botSeed));
    const stage0::BotSkill expert = *stage0::findBotSkill("expert");
    Rig rig(target().roomIndex);
    rig.bridge->enableBot(expert, botSeed);
    REQUIRE(rig.play(60 * 150, [&] { return rig.roomDone(); }) >= 0);
    rig.tick(10);

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ichor_dungeon_fight_slot_" + std::to_string(botSeed));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    SaveSlots slots(dir, SavePolicy{});
    const SaveContext context{7, 1, 42};
    REQUIRE(slots.save("quick", rig.scene, context));

    // A fresh process: the same systems installed in a new scene, then the load.
    Rig loaded(-1);
    const LoadResult result = slots.load("quick", loaded.scene, context);
    REQUIRE(result.status == LoadStatus::Loaded);
    CHECK(loaded.entry->inside(loaded.scene));
    loaded.tick(120);
    REQUIRE(loaded.fight->inDungeon());
    CHECK(loaded.roomDone());
    for (const DungeonEnemy& e : target().enemies) CHECK(loaded.killed(e));
    CHECK(clearedRooms(loaded.scene, loaded.entry->content()) == 1);
    CHECK(loaded.inRoom());
    loaded.tick(60 * 4);
    CHECK_FALSE(loaded.fight->engaged().has_value());
    CHECK_FALSE(loaded.enemy().alive());

    // The same scene without the kills does engage in that room, so the quiet above is the saved state's doing.
    Rig fresh(target().roomIndex);
    REQUIRE(fresh.play(60 * 10, [&] { return fresh.fight->engaged().has_value(); }) >= 0);
    std::filesystem::remove_all(dir);
}

TEST_CASE("a bad wake delay falls back to a usable one", "[dungeon_fight]")
{
    DungeonFightConfig config;
    config.wakeDelaySeconds = std::nan("");
    Rig rig(target().roomIndex, config);
    CHECK(rig.play(60 * 10, [&] { return rig.fight->engaged().has_value(); }) >= 0);
}
