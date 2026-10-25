#include "forged_world_fixture.h"

#include "ichor/core/input.h"
#include "ichor/data/data_set.h"
#include "ichor/save/save_slots.h"
#include "ichor/sim/scene.h"
#include "ichor/world/carry.h"
#include "ichor/world/dungeon_content.h"
#include "ichor/world/dungeon_entry.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/forged_walk_bot.h"
#include "ichor/world/forged_walk_route.h"
#include "ichor/world/parley.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <vector>

using namespace ichor;

// The errand end to end (SEED-674): talk, accept, walk to the dungeon, enter, pick the item up from its room, leave, walk back, turn in,
// with a save and a load in the middle. Nothing is teleported that the walk bot and the player's own body could not do: the bot proves
// the overworld legs, the physics body walks the dungeon room by room, and the scene is only placed where a bot leg ended.

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr int kGrid = 128;

// Found once by search (the hidden case "[.seed_search]" prints the next ones): worlds with a merchant, a dungeon whose item lies in an
// unguarded room the player can walk to, and overworld routes the walker can follow both ways.
constexpr std::uint64_t kSeeds[] = {1, 2, 3, 5};

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

struct World {
    DataSet data;
    ForgedContentSetup setup;
    std::unique_ptr<ForgedGame> game;
};

std::unique_ptr<World> worldOf(std::uint64_t seed)
{
    auto out = std::make_unique<World>();
    REQUIRE(out->data.loadDirectory(ICHOR_GAME_DATA_DIR));
    out->setup = loadForgedContentSetup(out->data, ICHOR_GAME_DATA_DIR);
    const ForgeJobResult forged = test::forgedWorldFixture(descFor(seed), kGrid, test::variantOfContentLists(out->setup.config));
    out->game = buildForgedGame(forged, descFor(seed), out->setup.inputs);
    INFO(out->game->error);
    REQUIRE(out->game->ok());
    return out;
}

ParleyWorld parleyWorld(const ForgedGame& game)
{
    ParleyWorld world{game.cast, game.pois, game.loot, game.spawn.x, game.spawn.z, game.desc.seed};
    world.siteOf = [&game](const PlacedLoot& item) { return itemSite(game, item.poiId, item.itemId); };
    return world;
}

// ---- the overworld legs ------------------------------------------------------------------------------------------------------

struct Leg {
    bool arrived = false;
    std::string why;
    glm::dvec3 end{0.0};
};

Leg walkLeg(const ForgedGame& game, double fromX, double fromZ, double toX, double toZ, double arriveMetres)
{
    const ForgedWalking walking(game.heightfield, game.waterField.get(), CapabilitySet{});
    const glm::dvec3 from{fromX, game.heightfield.heightAt(fromX, fromZ), fromZ};
    const glm::dvec3 goal{toX, game.heightfield.heightAt(toX, toZ), toZ};
    Leg leg;
    WalkRouteParams params;
    params.goalRadiusMetres = arriveMetres - 1.0;
    const WalkRoute route = findWalkRoute(walking, from, goal, params);
    if (!route.found) {
        leg.why = std::string("no route: ") + walkRouteFailureName(route.failure);
        return leg;
    }
    WalkBotParams bot;
    bot.arriveMetres = arriveMetres;
    const WalkResult result = walkTo(walking, from, goal, route.waypoints, bot);
    leg.arrived = result.outcome == WalkOutcome::Arrived;
    leg.end = result.end;
    if (!leg.arrived) leg.why = "walk stopped: " + result.report;
    return leg;
}

// ---- the game ---------------------------------------------------------------------------------------------------------------

struct Rig {
    ForgedGame& game;
    std::string stuck; // where the last walk through the rooms gave up
    Scene scene{7};
    CarrySystem carry;
    DungeonEntry entry;
    ParleySystem parley;

    Rig(ForgedGame& g, const DataSet& data)
        : game(g)
        , carry(outdoorLoot(g), catalogue(data))
        , entry(g, 0)
        , parley(parleyWorld(g), data, carry)
    {
        entry.install(scene);
        carry.setInteriorLoot([this] { return &entry.content().loot; });
        carry.install(scene);
        parley.install(scene);
        SpawnCommand player;
        player.id = StableId::of("player");
        player.name.value = "player";
        player.transform.position = {game.spawn.x, static_cast<double>(game.spawn.groundY) + 0.5, game.spawn.z};
        player.player = Player{};
        player.character = CharacterBody{};
        scene.enqueue(player);
        ground("spawn", game.spawn.x, game.spawn.groundY, game.spawn.z);
        for (int i = 0; i < 30; ++i) tick();
    }

    static CarryCatalogue catalogue(const DataSet& data)
    {
        std::vector<std::string> problems;
        return CarryCatalogue::fromData(data, problems);
    }

    void ground(const char* name, double x, double y, double z)
    {
        Transform transform;
        transform.position = {x, y - 1.0, z};
        scene.physics().addStaticBox(StableId::of(name), StaticBox{{400.0f, 1.0f, 400.0f}}, transform);
    }

    void tick(const InputFrame& input = {}) { scene.tick(kDt, input); }
    void settle()
    {
        for (int i = 0; i < 3; ++i) tick();
    }
    void press(std::uint32_t button)
    {
        InputFrame frame;
        frame.buttons = button;
        tick(frame);
        settle();
    }

    glm::dvec3 playerAt() const
    {
        const entt::entity e = scene.find(StableId::of("player"));
        REQUIRE((e != entt::null));
        return scene.registry().get<Transform>(e).position;
    }

    void placeAt(double x, double y, double z)
    {
        Transform transform;
        transform.position = {x, y, z};
        scene.enqueue(SetTransformCommand{StableId::of("player"), transform});
        settle();
    }

    // Steers the body at (x, z) until it is within `within` metres of it; false when it never gets there.
    bool steerTo(double x, double z, double within, int limit = 1500)
    {
        for (int i = 0; i < limit; ++i) {
            const glm::dvec3 at = playerAt();
            const double dx = x - at.x;
            const double dz = z - at.z;
            const double length = std::hypot(dx, dz);
            if (length <= within) return true;
            InputFrame frame;
            frame.moveX = static_cast<float>(dx / length);
            frame.moveY = static_cast<float>(-dz / length);
            tick(frame);
        }
        return false;
    }

    // Walks the body through the rooms from one to another along the grey-box's own doorways and ramps.
    bool walkRooms(graph::NodeId from, graph::NodeId to)
    {
        const DungeonGreyBox& box = entry.greyBox();
        const DungeonContent& content = entry.content();
        const auto steps = routeBetweenRooms(box, from, to);
        if (!steps) return false;
        constexpr double R = greybox::kRoomHalfM;
        graph::NodeId here = from;
        for (const RoomStep& step : *steps) {
            const glm::dvec3 a = content.room(here)->centre;
            const glm::dvec3 b = content.room(step.room)->centre;
            std::vector<glm::dvec2> via;
            if (step.link == RoomLink::Door) {
                via.push_back({(a.x + b.x) * 0.5, (a.z + b.z) * 0.5});
            } else {
                const glm::dvec3& lower = step.link == RoomLink::RampUp ? a : b;
                const double xs = rampOffset(box, lower);
                if (step.link == RoomLink::RampUp) {
                    via = {{a.x, a.z - (R - 0.5)}, {a.x + xs, a.z - (R - 0.5)}, {a.x + xs, a.z + (R - 0.4)}, {a.x, a.z + (R - 0.4)}}; // off the top sideways: the hole lies over the whole ramp strip
                } else {
                    via = {{a.x, a.z + (R - 0.4)}, {a.x + xs, a.z + (R - 0.4)}, {a.x + xs, a.z - (R - 0.5)}, {a.x, a.z - (R - 0.5)}};
                }
            }
            via.push_back({b.x, b.z});
            for (const glm::dvec2& point : via) {
                if (!steerTo(point.x, point.y, 0.5)) {
                    stuck = std::format("stopped at ({:.1f}, {:.2f}, {:.1f}) on the way to ({:.1f}, {:.1f}) from room {} to room {}", playerAt().x, playerAt().y, playerAt().z, point.x, point.y,
                        here, step.room);
                    return false;
                }
            }
            if (std::abs(playerAt().y - b.y) > 1.5) {
                stuck = std::format("not on the floor of room {} (feet at y {:.2f}, floor {:.2f}) coming from room {}", step.room, playerAt().y, b.y, here);
                return false;
            }
            here = step.room;
        }
        return true;
    }

    // Where the ramp of a room lies, as an offset from its centre along x: the plank is the only tilted floor piece.
    static double rampOffset(const DungeonGreyBox& box, const glm::dvec3& lowerCentre)
    {
        for (const GreyBoxPiece& piece : box.pieces) {
            if (piece.kind != GreyBoxKind::Floor || std::abs(piece.rotation.w - 1.0f) < 1e-4f) continue;
            if (std::abs(piece.centre.z - lowerCentre.z) < 0.5 && std::abs(piece.centre.y - (lowerCentre.y + 2.25)) < 0.5 && std::abs(piece.centre.x - lowerCentre.x) < greybox::kRoomHalfM) {
                return piece.centre.x - lowerCentre.x;
            }
        }
        return 0.0;
    }

    const graph::NodeId entryRoom() const
    {
        for (const DungeonRoom& room : entry.content().rooms) {
            if (room.type == "entry") return room.id;
        }
        return 0;
    }

    const DungeonRoom* roomOfPlayer() const { return entry.content().roomAt(playerAt()); }
};

std::string journalText(const Rig& rig)
{
    std::string out;
    for (const QuestLogEntry& entry : rig.parley.questLog(rig.scene)) {
        out += entry.title + (entry.stage == QuestStage::Done ? " [done] " : " [open] ");
        for (const QuestObjective& objective : entry.objectives) out += (objective.done ? "[x] " : "[ ] ") + objective.text + "; ";
        out += entry.outcome;
    }
    return out;
}

void enterDungeon(Rig& rig, const Leg& toDungeon, const PlacedPoi& poi)
{
    rig.ground("dungeon", poi.x, poi.groundY, poi.z);
    rig.placeAt(toDungeon.end.x, static_cast<double>(poi.groundY) + 0.5, toDungeon.end.z);
    REQUIRE_FALSE(rig.entry.prompt(rig.scene).empty());
    rig.press(InputFrame::Confirm);
    for (int i = 0; i < 150 && !(rig.entry.inside(rig.scene) && !rig.entry.content().empty()); ++i) rig.tick();
    REQUIRE(rig.entry.inside(rig.scene));
    for (int i = 0; i < 60; ++i) rig.tick();
}

// Plays the whole errand in one world and returns how many ramps the way to the item climbs. Every step that can fail has its own check,
// so a red run names the step.
std::size_t playthrough(std::uint64_t seed)
{
    INFO("seed " << seed);
    const std::unique_ptr<World> world = worldOf(seed);
    ForgedGame& game = *world->game;
    auto rig = std::make_unique<Rig>(game, world->data);

    const QuestBinding binding = rig->parley.binding();
    INFO("no errand: " << binding.reason);
    REQUIRE(binding.bound);
    // The item lies inside a room of the dungeon, one the player can walk to, with nobody standing guard and not at the door.
    const PlacedLoot* mouth = game.loot->find(binding.itemId);
    REQUIRE(mouth != nullptr);
    const ItemSite site = itemSite(game, binding.dungeonPoi, binding.itemId);
    REQUIRE(site.inside);
    REQUIRE(site.reachable);
    CHECK_FALSE(site.guarded);
    CHECK_FALSE(site.crowded);
    CHECK(site.roomType != "entry");
    CHECK(binding.roomType == site.roomType);
    REQUIRE(outdoorLoot(game)->find(binding.itemId) == nullptr);

    const NpcSpawn& giver = *game.cast->find(binding.giverNpc);
    const PlacedPoi& poi = *game.pois->find(binding.dungeonPoi);

    // Talk and accept at the merchant, after walking there from the spawn.
    const Leg toGiver = walkLeg(game, game.spawn.x, game.spawn.z, giver.x, giver.z, 4.0);
    INFO(toGiver.why);
    REQUIRE(toGiver.arrived);
    rig->ground("giver", giver.x, giver.groundY, giver.z);
    rig->placeAt(giver.x + 1.0, static_cast<double>(giver.groundY) + 0.5, giver.z);
    rig->press(InputFrame::Interact);
    REQUIRE(rig->parley.open());
    REQUIRE(rig->parley.view(rig->scene).choices.front().key == "accept");
    rig->parley.choose(rig->scene, "accept");
    rig->settle();
    REQUIRE(rig->parley.stageOf(rig->scene) == QuestStage::Active);
    CHECK(journalText(*rig).find("[x] Hear out") != std::string::npos);
    rig->parley.close();

    // Walk to the dungeon and in.
    const Leg toDungeon = walkLeg(game, giver.x, giver.z, poi.x, poi.z, 4.0);
    INFO(toDungeon.why);
    REQUIRE(toDungeon.arrived);
    enterDungeon(*rig, toDungeon, poi);
    const glm::dvec3 outsideSpot = {toDungeon.end.x, static_cast<double>(poi.groundY) + 0.5, toDungeon.end.z};

    // Into the item's room, on foot.
    const graph::NodeId entryRoom = rig->entryRoom();
    REQUIRE(entryRoom != 0);
    const graph::NodeId itemRoom = site.room;
    std::size_t ramps = 0;
    for (const RoomStep& step : *routeBetweenRooms(rig->entry.greyBox(), entryRoom, itemRoom)) ramps += step.link == RoomLink::Door ? 0 : 1;
    const bool inRoom = rig->walkRooms(entryRoom, itemRoom);
    INFO(rig->stuck);
    REQUIRE(inRoom);
    REQUIRE(rig->roomOfPlayer() != nullptr);
    CHECK(rig->roomOfPlayer()->id == itemRoom);
    const PlacedLoot item = *std::find_if(rig->entry.content().loot.begin(), rig->entry.content().loot.end(), [&](const PlacedLoot& l) { return l.itemId == binding.itemId; });
    REQUIRE_FALSE(CarrySystem::isTaken(rig->carry.carried(rig->scene), binding.itemId));

    // A save made here, before the pickup, comes back inside the dungeon with the errand open.
    const std::filesystem::path dir = test::tempPath("ichor_parley_playthrough");
    std::filesystem::remove_all(dir);
    SaveSlots slots(dir, SavePolicy{});
    const SaveContext context{7, 1, 42};
    REQUIRE(slots.save("inside", rig->scene, context));
    const std::string before = journalText(*rig);
    auto loaded = std::make_unique<Rig>(game, world->data);
    REQUIRE(slots.load("inside", loaded->scene, context).status == LoadStatus::Loaded);
    for (int i = 0; i < 90; ++i) loaded->tick();
    REQUIRE(loaded->entry.inside(loaded->scene));
    rig = std::move(loaded);
    rig->ground("giver", giver.x, giver.groundY, giver.z);
    rig->ground("dungeon", poi.x, poi.groundY, poi.z);
    CHECK(journalText(*rig) == before);
    REQUIRE(rig->parley.stageOf(rig->scene) == QuestStage::Active);

    // Pick it up: the second objective is met in the journal at once.
    REQUIRE(rig->steerTo(item.x, item.z, 0.3));
    REQUIRE(rig->carry.itemWithinReach(rig->scene));
    rig->press(InputFrame::Interact);
    REQUIRE(rig->carry.countOf(rig->scene, binding.itemId) == 1);
    REQUIRE(rig->parley.stageOf(rig->scene) == QuestStage::Found);
    {
        const std::vector<QuestLogEntry> log = rig->parley.questLog(rig->scene);
        REQUIRE(log.size() == 1);
        REQUIRE(log[0].objectives.size() == 3);
        CHECK(log[0].objectives[1].done);
        CHECK_FALSE(log[0].objectives[2].done);
    }

    // Out again, to where the player went in.
    const bool outAgain = rig->walkRooms(itemRoom, entryRoom);
    INFO(rig->stuck);
    REQUIRE(outAgain);
    REQUIRE(rig->steerTo(rig->entry.greyBox().exitPoint.x, rig->entry.greyBox().exitPoint.z, 1.5));
    REQUIRE(rig->entry.prompt(rig->scene).find("leave") != std::string::npos);
    rig->press(InputFrame::Confirm);
    for (int i = 0; i < 150 && rig->entry.inside(rig->scene); ++i) rig->tick();
    REQUIRE_FALSE(rig->entry.inside(rig->scene));
    CHECK(glm::length(rig->playerAt() - outsideSpot) < 1.5);
    CHECK(rig->carry.countOf(rig->scene, binding.itemId) == 1);
    CHECK(rig->parley.stageOf(rig->scene) == QuestStage::Found);

    // Back to the merchant, who takes the item and pays.
    const Leg back = walkLeg(game, rig->playerAt().x, rig->playerAt().z, giver.x, giver.z, 4.0);
    INFO(back.why);
    REQUIRE(back.arrived);
    const std::uint64_t coin = rig->parley.coin(rig->scene);
    rig->placeAt(giver.x + 1.0, static_cast<double>(giver.groundY) + 0.5, giver.z);
    rig->press(InputFrame::Interact);
    REQUIRE(rig->parley.open());
    REQUIRE(rig->parley.view(rig->scene).choices.front().key == "turnin");
    rig->parley.choose(rig->scene, "turnin");
    rig->settle();
    REQUIRE(rig->parley.stageOf(rig->scene) == QuestStage::Done);
    CHECK(rig->carry.countOf(rig->scene, binding.itemId) == 0);
    CHECK(rig->parley.coin(rig->scene) == coin + rig->parley.quest()->rewardCoin);
    CHECK(rig->carry.countOf(rig->scene, rig->parley.quest()->rewardItem) == rig->parley.quest()->rewardCount);
    const std::vector<QuestLogEntry> log = rig->parley.questLog(rig->scene);
    REQUIRE(log.size() == 1);
    for (const QuestObjective& objective : log[0].objectives) CHECK(objective.done);
    CHECK_FALSE(log[0].outcome.empty());

    // The finished errand survives a save: the same state, the same journal, and the item is not lying in the dungeon again.
    const std::uint64_t hash = rig->scene.stateHash();
    const std::string finished = journalText(*rig);
    REQUIRE(slots.save("done", rig->scene, context));
    Rig after(game, world->data);
    REQUIRE(slots.load("done", after.scene, context).status == LoadStatus::Loaded);
    CHECK(after.scene.stateHash() == hash);
    CHECK(journalText(after) == finished);
    CHECK(after.parley.stageOf(after.scene) == QuestStage::Done);
    CHECK(CarrySystem::isTaken(after.carry.carried(after.scene), binding.itemId));
    std::filesystem::remove_all(dir);
    return ramps;
}

}

TEST_CASE("the errand can be finished on foot in the world of seed 1", "[parley][quest][playthrough]")
{
    playthrough(kSeeds[0]);
}

TEST_CASE("the errand can be finished on foot in the world of seed 2", "[parley][quest][playthrough]")
{
    playthrough(kSeeds[1]);
}

TEST_CASE("the errand can be finished on foot in the world of seed 3", "[parley][quest][playthrough]")
{
    playthrough(kSeeds[2]);
}

TEST_CASE("the errand can be finished on foot in the world of seed 5", "[parley][quest][playthrough]")
{
    playthrough(kSeeds[3]);
}

// The walk through the rooms is proved on the dungeon whose rooms need the most ramps (found by the search case below): every room the
// route calls reachable is walked to from the entry room by the body itself, up and down its ramps as well as through its doorways.
TEST_CASE("every room of a dungeon that the route reaches can be walked to, up and down its ramps", "[parley][quest][playthrough]")
{
    const std::unique_ptr<World> world = worldOf(8);
    ForgedGame& game = *world->game;
    const PlacedPoi* poi = nullptr;
    for (const PlacedPoi& candidate : game.pois->all()) {
        if (candidate.kind == PlacedPoiKind::Dungeon) {
            poi = &candidate;
            break;
        }
    }
    REQUIRE(poi != nullptr);
    Rig rig(game, world->data);
    rig.ground("dungeon", poi->x, poi->groundY, poi->z);
    rig.placeAt(poi->x - 3.0, static_cast<double>(poi->groundY) + 0.5, poi->z);
    REQUIRE_FALSE(rig.entry.prompt(rig.scene).empty());
    rig.press(InputFrame::Confirm);
    for (int i = 0; i < 150 && !(rig.entry.inside(rig.scene) && !rig.entry.content().empty()); ++i) rig.tick();
    REQUIRE(rig.entry.inside(rig.scene));
    for (int i = 0; i < 60; ++i) rig.tick();

    const DungeonContent& content = rig.entry.content();
    const graph::NodeId entryRoom = rig.entryRoom();
    REQUIRE(entryRoom != 0);
    std::size_t walked = 0;
    std::size_t viaRamp = 0;
    for (const DungeonRoom& room : content.rooms) {
        const auto route = routeBetweenRooms(rig.entry.greyBox(), entryRoom, room.id);
        CHECK(route.has_value() == room.reachable);
        if (!route) continue;
        const glm::dvec3 start = content.room(entryRoom)->centre;
        rig.placeAt(start.x, start.y + 0.5, start.z);
        const bool arrived = rig.walkRooms(entryRoom, room.id);
        INFO("room " << room.id << " (" << room.type << "): " << rig.stuck);
        CHECK(arrived);
        walked += arrived ? 1 : 0;
        viaRamp += std::any_of(route->begin(), route->end(), [](const RoomStep& step) { return step.link != RoomLink::Door; }) ? 1 : 0;
    }
    CHECK(walked > 1);
    CHECK(viaRamp >= 1);
}

// Prints the seeds that qualify; run it by name to find new ones. Not part of any suite.
TEST_CASE("which seeds give a playable errand", "[.seed_search]")
{
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        const std::unique_ptr<World> world = worldOf(seed);
        ForgedGame& game = *world->game;
        CarrySystem carry(outdoorLoot(game), Rig::catalogue(world->data));
        ParleySystem parley(parleyWorld(game), world->data, carry);
        const QuestBinding& binding = parley.binding();
        if (!binding.bound) {
            std::printf("seed %llu: no errand (%s)\n", static_cast<unsigned long long>(seed), binding.reason.c_str());
            continue;
        }
        const ItemSite site = itemSite(game, binding.dungeonPoi, binding.itemId);
        const NpcSpawn& giver = *game.cast->find(binding.giverNpc);
        const PlacedPoi& poi = *game.pois->find(binding.dungeonPoi);
        std::size_t dungeonIndex = 0;
        for (const PlacedPoi& other : game.pois->all()) {
            if (other.kind != PlacedPoiKind::Dungeon) continue;
            if (other.id == poi.id) break;
            ++dungeonIndex;
        }
        // For the screenshots: ichor --forged --seed S --dungeon <index> --enter-dungeon --dungeon-room <room>.
        std::size_t roomIndex = 0;
        std::size_t ramps = 0;
        if (const DungeonLayoutResult layout = game.dungeons->layout(poi.id); layout.ok()) {
            const DungeonGreyBox box = buildDungeonGreyBox(*layout.dungeon, glm::dvec3(0.0));
            const DungeonContent content = buildDungeonContent(*layout.dungeon, box, lootAtPoi(*game.loot, poi.id));
            for (std::size_t i = 0; i < content.rooms.size(); ++i) {
                if (content.rooms[i].id == site.room) roomIndex = i;
            }
            const auto entryRoom = std::find_if(content.rooms.begin(), content.rooms.end(), [](const DungeonRoom& r) { return r.type == "entry"; });
            if (entryRoom != content.rooms.end()) {
                if (const auto route = routeBetweenRooms(box, entryRoom->id, site.room)) {
                    for (const RoomStep& step : *route) ramps += step.link == RoomLink::Door ? 0 : 1;
                }
            }
        }
        const Leg out = walkLeg(game, giver.x, giver.z, poi.x, poi.z, 4.0);
        const Leg home = walkLeg(game, game.spawn.x, game.spawn.z, giver.x, giver.z, 4.0);
        std::printf("seed %llu: inside=%d reachable=%d guarded=%d crowded=%d room=%s dungeon=%.0f m (index %zu, room index %zu, ramps %zu) out=%s home=%s\n", static_cast<unsigned long long>(seed), site.inside, site.reachable,
            site.guarded, site.crowded, site.roomType.c_str(), std::hypot(poi.x - giver.x, poi.z - giver.z), dungeonIndex, roomIndex, ramps, out.arrived ? "ok" : out.why.c_str(), home.arrived ? "ok" : home.why.c_str());
        std::fflush(stdout);
    }
}
