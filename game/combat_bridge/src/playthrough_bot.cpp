#include "ichor/combat_bridge/playthrough_bot.h"

#include "ichor/combat_bridge/combat_bridge.h"
#include "ichor/combat_bridge/combat_site.h"
#include "ichor/combat_bridge/dungeon_fight.h"
#include "ichor/stage0/anchor.h"
#include "ichor/stage0/artillery.h"
#include "ichor/core/input.h"
#include "ichor/save/save_slots.h"
#include "ichor/sim/scene.h"
#include "ichor/world/carry.h"
#include "ichor/world/dungeon_content.h"
#include "ichor/world/dungeon_entry.h"
#include "ichor/world/dungeon_greybox.h"
#include "ichor/world/forge_job.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/forged_walk_bot.h"
#include "ichor/world/forged_walk_route.h"
#include "ichor/world/forged_walking.h"
#include "ichor/world/parley.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace ichor {

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr double kArriveMetres = 4.0;
constexpr int kSteerTickLimit = 1500; // steering ticks per waypoint, 25 s of scene time
constexpr const char* kSlot = "playthrough";

using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0)
{
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

struct World {
    ForgedContentSetup setup;
    std::unique_ptr<ForgedGame> game;
    std::string error;
};

World forgeWorld(std::uint64_t seed, const DataSet& data, const std::filesystem::path& dataDir, const PlaythroughParams& params)
{
    World out;
    out.setup = loadForgedContentSetup(data, dataDir);
    ForgeJob job(descFor(seed), params.grid, {}, out.setup.config);
    if (!job.start()) {
        out.error = "the forge job did not start";
        return out;
    }
    const auto t0 = Clock::now();
    while (job.state() == ForgeJobState::Running && since(t0) < params.maxForgeSeconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (job.state() == ForgeJobState::Running) {
        out.error = std::format("the forge took longer than {:.0f} s", params.maxForgeSeconds);
        return out;
    }
    if (job.state() != ForgeJobState::Done) {
        out.error = "the forge failed: " + job.error();
        return out;
    }
    out.game = buildForgedGame(*job.result(), descFor(seed), out.setup.inputs);
    if (!out.game->ok()) {
        out.error = "the game could not be built: " + out.game->error;
        out.game.reset();
    }
    return out;
}

// ---- the overworld legs: the walk bot over the walking rules ------------------------------------------------------------------

struct Leg {
    bool arrived = false;
    bool stalled = false; // timed out while still moving
    std::string why;
    glm::dvec3 end{0.0};
    double trip = 0.0;
    double walked = 0.0;
    double seconds = 0.0;
};

Leg walkLeg(const ForgedGame& game, double fromX, double fromZ, double toX, double toZ)
{
    const ForgedWalking walking(game.heightfield, game.waterField.get(), CapabilitySet{});
    const glm::dvec3 from{fromX, game.heightfield.heightAt(fromX, fromZ), fromZ};
    const glm::dvec3 goal{toX, game.heightfield.heightAt(toX, toZ), toZ};
    Leg leg;
    leg.end = from;
    leg.trip = std::hypot(toX - fromX, toZ - fromZ);
    WalkRouteParams params;
    params.goalRadiusMetres = kArriveMetres - 1.0;
    const WalkRoute route = findWalkRoute(walking, from, goal, params);
    if (!route.found) {
        leg.why = std::string("no route: ") + walkRouteFailureName(route.failure);
        return leg;
    }
    WalkBotParams bot;
    bot.arriveMetres = kArriveMetres;
    const WalkResult result = walkTo(walking, from, goal, route.waypoints, bot);
    leg.arrived = result.outcome == WalkOutcome::Arrived;
    leg.end = result.end;
    leg.walked = result.metresWalked;
    leg.seconds = result.seconds;
    if (!leg.arrived) {
        leg.stalled = result.outcome == WalkOutcome::TimedOut;
        leg.why = leg.stalled ? std::format("timed out after {:.0f} s of scene time while still moving, {:.0f} m short", result.seconds, result.metresLeft)
                              : "blocked: " + result.report;
    }
    return leg;
}

std::string describeLeg(const Leg& leg)
{
    return std::format("trip {:.0f} m, walked {:.0f} m in {:.0f} s of scene time", leg.trip, leg.walked, leg.seconds);
}

// ---- the game: the app's systems in one headless scene ------------------------------------------------------------------------

CarryCatalogue catalogueOf(const DataSet& data)
{
    std::vector<std::string> problems;
    return CarryCatalogue::fromData(data, problems);
}

ParleyWorld parleyWorldOf(const ForgedGame& game)
{
    ParleyWorld world{game.cast, game.pois, game.loot, game.spawn.x, game.spawn.z, game.desc.seed};
    world.siteOf = [&game](const PlacedLoot& item) { return itemSite(game, item.poiId, item.itemId); };
    return world;
}

std::string journalText(const ParleySystem& parley, const Scene& scene)
{
    std::string out;
    for (const QuestLogEntry& entry : parley.questLog(scene)) {
        out += entry.title + (entry.stage == QuestStage::Done ? " [done] " : " [open] ");
        for (const QuestObjective& objective : entry.objectives) out += (objective.done ? "[x] " : "[ ] ") + objective.text + "; ";
        out += entry.outcome;
    }
    return out;
}

enum class Move : std::uint8_t { Arrived, Stuck, Died };

// What the stall watch compares between ticks: when none of it changes for stallSeconds, nothing is happening.
struct Pulse {
    glm::dvec3 at{0.0};
    float health = 0.0f;
    float foeHealth = 0.0f;
    std::size_t cleared = 0;
    bool engaged = false;

    bool same(const Pulse& other) const
    {
        return glm::distance(at, other.at) < 0.02 && health == other.health && foeHealth == other.foeHealth && cleared == other.cleared && engaged == other.engaged;
    }
};

// The app's order of installing: bridge, dungeon entry, fight, then the carry and the parley on top.
struct Rig {
    ForgedGame& game;
    const PlaythroughParams& params;
    Scene scene{7};
    CombatSite site;
    std::unique_ptr<CombatBridge> bridge;
    std::unique_ptr<DungeonEntry> entry;
    std::unique_ptr<DungeonFight> fight;
    CarrySystem carry;
    ParleySystem parley;

    std::string stall;  // why the last dungeon move gave up
    int stepTicks = 0;  // ticks since beginStep
    int quietTicks = 0; // ticks the pulse has not changed
    Pulse last;

    Rig(ForgedGame& g, const DataSet& data, const PlaythroughParams& p)
        : game(g)
        , params(p)
        , carry(outdoorLoot(g), catalogueOf(data))
        , parley(parleyWorldOf(g), data, carry)
    {
        site = findCombatSite(game);
        const glm::dvec3 start{game.spawn.x, static_cast<double>(game.spawn.groundY) + 0.5, game.spawn.z};
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
        bridge->setGroundCheck([](glm::dvec3) { return false; }); // the world's foe never spawns: the fight this bot drives is the dungeon's
        bridge->install(scene, StableId::of("player"), start);
        entry = std::make_unique<DungeonEntry>(game, 0);
        entry->install(scene);
        fight = std::make_unique<DungeonFight>(*bridge, *entry);
        fight->install(scene, StableId::of("player"));
        carry.setInteriorLoot([this] { return &entry->content().loot; });
        carry.install(scene);
        parley.install(scene);
        if (const std::optional<stage0::BotSkill> skill = stage0::findBotSkill(params.skill)) {
            bridge->enableBot(*skill, params.botSeed);
        }
        ground("spawn", game.spawn.x, game.spawn.groundY, game.spawn.z);
        for (int i = 0; i < 30; ++i) tick();
    }

    // The headless scene has no terrain: a slab stands in for the ground where a leg of the trip ends.
    void ground(const char* name, double x, double y, double z)
    {
        Transform transform;
        transform.position = {x, y - 1.0, z};
        scene.physics().addStaticBox(StableId::of(name), StaticBox{{400.0f, 1.0f, 400.0f}}, transform);
    }

    void tick(const InputFrame& input = {})
    {
        scene.tick(kDt, input);
        ++stepTicks;
    }
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
    void beginStep()
    {
        stepTicks = 0;
        quietTicks = 0;
        last = pulse();
        stall.clear();
    }
    double stepSeconds() const { return stepTicks * kDt; }

    entt::entity player() const { return scene.find(StableId::of("player")); }
    glm::dvec3 playerAt() const
    {
        const entt::entity e = player();
        return e == entt::null ? glm::dvec3(0.0) : scene.registry().get<Transform>(e).position;
    }
    const stage0::Combatant* combatantOf(entt::entity e) const { return e == entt::null ? nullptr : scene.registry().try_get<stage0::Combatant>(e); }
    bool alive() const
    {
        const stage0::Combatant* fighter = combatantOf(player());
        return fighter == nullptr || fighter->alive();
    }
    Pulse pulse() const
    {
        Pulse now;
        now.at = playerAt();
        if (const stage0::Combatant* fighter = combatantOf(player())) now.health = fighter->health;
        if (const stage0::Combatant* foe = combatantOf(scene.find(bridge->stage().foeId()))) now.foeHealth = foe->health;
        now.cleared = clearedRooms(scene, entry->content());
        now.engaged = fight->engaged().has_value();
        return now;
    }

    void placeAt(double x, double y, double z)
    {
        Transform transform;
        transform.position = {x, y, z};
        scene.enqueue(SetTransformCommand{StableId::of("player"), transform});
        settle();
    }

    // One tick of the dungeon: the fight bot's frame while a foe is engaged or the player lies dead, else `steer`. False when
    // nothing has changed for stallSeconds or the step is over its time, with `stall` saying so.
    bool advance(const InputFrame& steer)
    {
        if (fight->engaged().has_value() || !alive()) {
            tick(bridge->botInput(scene));
        } else {
            tick(steer);
        }
        const Pulse now = pulse();
        quietTicks = now.same(last) ? quietTicks + 1 : 0;
        last = now;
        if (quietTicks * kDt >= params.stallSeconds) {
            stall = std::format("nothing moved for {:.0f} s at ({:.1f}, {:.2f}, {:.1f}): {}", params.stallSeconds, now.at.x, now.at.y, now.at.z, describeStandstill());
            return false;
        }
        if (stepSeconds() > params.maxStepSeconds) {
            stall = std::format("the step ran over {:.0f} s of scene time at ({:.1f}, {:.2f}, {:.1f})", params.maxStepSeconds, now.at.x, now.at.y, now.at.z);
            return false;
        }
        return true;
    }

    // What the player and the foe were doing when nothing moved, for the stall report.
    std::string describeStandstill() const
    {
        std::string text;
        const entt::entity me = player();
        if (const stage0::Combatant* fighter = combatantOf(me)) {
            text += std::format("player {} with {:.0f} hp", stage0::name(fighter->combatState()), fighter->health);
        } else {
            text += "player without a combatant";
        }
        if (const CharacterBody* body = me == entt::null ? nullptr : scene.registry().try_get<CharacterBody>(me)) {
            text += std::format(", intent {:.2f} m/s", glm::length(body->intent));
        }
        const entt::entity foe = scene.find(bridge->stage().foeId());
        if (const stage0::Combatant* f = combatantOf(foe)) {
            const glm::dvec3 there = scene.registry().get<Transform>(foe).position;
            text += std::format(", foe {} with {:.0f} hp {:.1f} m away", f->alive() ? stage0::name(f->combatState()) : "dead", f->health, std::hypot(there.x - playerAt().x, there.z - playerAt().z));
            if (const auto* artillery = scene.registry().try_get<stage0::ArtilleryBrain>(foe)) text += std::format(" (artillery {})", stage0::name(artillery->artilleryState()));
            if (const auto* anchor = scene.registry().try_get<stage0::AnchorBrain>(foe)) text += std::format(" (anchor {})", stage0::name(anchor->anchorState()));
        }
        if (const std::optional<std::size_t> e = fight->engaged()) text += std::format(", engaged with enemy {} of room {}", entry->content().enemies[*e].slot, entry->content().enemies[*e].room);
        const DungeonRoom* room = roomOfPlayer();
        text += room != nullptr ? std::format(", in room {} ({})", room->id, room->type) : ", in no room";
        return text;
    }

    // Steers the body at (x, z) until it is within `within` metres, fighting whatever wakes on the way. Died once the player has
    // respawned after a death; Stuck when the steering ticks run out or advance says the scene stalled.
    Move steerTo(double x, double z, double within)
    {
        const int respawnsBefore = bridge->respawns();
        int steered = 0;
        while (steered < kSteerTickLimit) {
            if (bridge->respawns() != respawnsBefore) return Move::Died;
            if (fight->engaged().has_value() || !alive()) {
                if (!advance({})) return Move::Stuck;
                continue;
            }
            const glm::dvec3 at = playerAt();
            const double dx = x - at.x;
            const double dz = z - at.z;
            const double length = std::hypot(dx, dz);
            if (length <= within) return Move::Arrived;
            InputFrame frame;
            frame.moveX = static_cast<float>(dx / length);
            frame.moveY = static_cast<float>(-dz / length);
            if (!advance(frame)) return Move::Stuck;
            ++steered;
        }
        const glm::dvec3 at = playerAt();
        stall = std::format("stopped at ({:.1f}, {:.2f}, {:.1f}) on the way to ({:.1f}, {:.1f})", at.x, at.y, at.z, x, z);
        return Move::Stuck;
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

    const DungeonRoom* roomOfPlayer() const { return entry->content().roomAt(playerAt()); }

    graph::NodeId entryRoom() const
    {
        for (const DungeonRoom& room : entry->content().rooms) {
            if (room.type == "entry") return room.id;
        }
        return 0;
    }

    // Walks the body to room `to` through the grey-box's own doorways and ramps, one room at a time from wherever the player
    // stands, so a death (which puts the player back in the entry room) just means more rooms to walk. False with `stall` set.
    bool walkRooms(graph::NodeId to, int& deaths)
    {
        const DungeonGreyBox& box = entry->greyBox();
        const DungeonContent& content = entry->content();
        constexpr double R = greybox::kRoomHalfM;
        for (;;) {
            const DungeonRoom* here = roomOfPlayer();
            if (here == nullptr) {
                const glm::dvec3 at = playerAt();
                stall = std::format("not in any room at ({:.1f}, {:.2f}, {:.1f})", at.x, at.y, at.z);
                return false;
            }
            if (here->id == to) return true;
            const std::optional<std::vector<RoomStep>> steps = routeBetweenRooms(box, here->id, to);
            if (!steps || steps->empty()) {
                stall = std::format("no route from room {} to room {}", here->id, to);
                return false;
            }
            const RoomStep& step = steps->front();
            const glm::dvec3 a = here->centre;
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
            Move move = Move::Arrived;
            for (const glm::dvec2& point : via) {
                move = steerTo(point.x, point.y, 0.5);
                if (move != Move::Arrived) break;
            }
            if (move == Move::Died) {
                if (++deaths > params.maxDeaths) {
                    stall = std::format("died {} times on the way to room {}", deaths, to);
                    return false;
                }
                continue;
            }
            if (move == Move::Stuck) {
                stall += std::format(", from room {} to room {}", here->id, step.room);
                return false;
            }
            if (std::abs(playerAt().y - b.y) > 1.5) {
                stall = std::format("not on the floor of room {} (feet at y {:.2f}, floor {:.2f}) coming from room {}", step.room, playerAt().y, b.y, here->id);
                return false;
            }
        }
    }

    // Stands in `room` and fights whoever wakes until every enemy of it is dead; after a death, walks back and carries on.
    bool clearRoom(graph::NodeId room, int& deaths)
    {
        const DungeonContent& content = entry->content();
        while (!roomCleared(scene, content, room)) {
            if (!walkRooms(room, deaths)) return false;
            const int respawnsBefore = bridge->respawns();
            while (!roomCleared(scene, content, room) && bridge->respawns() == respawnsBefore) {
                if (!advance({})) return false;
            }
            if (bridge->respawns() != respawnsBefore && ++deaths > params.maxDeaths) {
                stall = std::format("died {} times in room {}", deaths, room);
                return false;
            }
        }
        return true;
    }
};

std::string describeRoom(const DungeonContent& content, graph::NodeId room)
{
    const DungeonRoom* r = content.room(room);
    return r == nullptr ? std::format("room {}", room) : std::format("room {} ({})", room, r->type);
}

const char* roleName(DungeonEnemyRole role)
{
    switch (role) {
    case DungeonEnemyRole::Skirmisher: return "skirmisher";
    case DungeonEnemyRole::Anchor: return "anchor";
    case DungeonEnemyRole::Artillery: return "artillery";
    }
    return "unknown";
}

struct Runner {
    PlaythroughResult& result;
    Rig* rig = nullptr;
    Clock::time_point stepStart = Clock::now();

    void begin(PlaythroughStep step)
    {
        stepStart = Clock::now();
        result.steps[static_cast<std::size_t>(step)].step = step;
        if (rig != nullptr) rig->beginStep();
    }
    bool end(PlaythroughStep step, PlaythroughStatus status, std::string detail)
    {
        PlaythroughStepResult& r = result.steps[static_cast<std::size_t>(step)];
        r.status = status;
        r.detail = std::move(detail);
        r.wallSeconds = since(stepStart);
        r.simSeconds = rig != nullptr ? rig->stepSeconds() : 0.0;
        return status == PlaythroughStatus::Done || status == PlaythroughStatus::Unsupported;
    }
    bool done(PlaythroughStep step, std::string detail) { return end(step, PlaythroughStatus::Done, std::move(detail)); }
    bool fail(PlaythroughStep step, std::string detail) { return end(step, PlaythroughStatus::Failed, std::move(detail)); }
    // A dungeon move that gave up: the rig's stall text says whether the scene stood still or the step could not be done.
    bool giveUp(PlaythroughStep step, const std::string& what)
    {
        const bool stood = rig->stall.find("nothing moved") != std::string::npos || rig->stall.find("ran over") != std::string::npos
            || rig->stall.find("stopped at") != std::string::npos;
        return end(step, stood ? PlaythroughStatus::Stalled : PlaythroughStatus::Failed, what + ": " + rig->stall);
    }
};

}

const char* playthroughStepName(PlaythroughStep step)
{
    switch (step) {
    case PlaythroughStep::Walk: return "walk";
    case PlaythroughStep::Talk: return "talk";
    case PlaythroughStep::Accept: return "accept";
    case PlaythroughStep::Fight: return "fight";
    case PlaythroughStep::Loot: return "loot";
    case PlaythroughStep::Save: return "save";
    case PlaythroughStep::Quit: return "quit";
    case PlaythroughStep::Load: return "load";
    }
    return "unknown";
}

const char* playthroughStatusName(PlaythroughStatus status)
{
    switch (status) {
    case PlaythroughStatus::NotRun: return "NOT RUN";
    case PlaythroughStatus::Done: return "ok";
    case PlaythroughStatus::Failed: return "FAILED";
    case PlaythroughStatus::Stalled: return "STALLED";
    case PlaythroughStatus::Unsupported: return "UNSUPPORTED";
    }
    return "unknown";
}

bool PlaythroughResult::completed() const
{
    return worldError.empty() && std::all_of(steps.begin(), steps.end(), [](const PlaythroughStepResult& s) {
        return s.status == PlaythroughStatus::Done || s.status == PlaythroughStatus::Unsupported;
    });
}

std::string PlaythroughResult::summary() const
{
    if (!worldError.empty()) return "no world: " + worldError;
    std::string out;
    for (const PlaythroughStepResult& s : steps) {
        out += std::format("{}{} {}", out.empty() ? "" : ", ", playthroughStepName(s.step), playthroughStatusName(s.status));
    }
    return out;
}

PlaythroughResult playThrough(std::uint64_t seed, const DataSet& data, const std::filesystem::path& dataDir, const PlaythroughParams& params)
{
    PlaythroughResult result;
    result.seed = seed;
    for (std::size_t i = 0; i < kPlaythroughSteps; ++i) result.steps[i].step = static_cast<PlaythroughStep>(i);
    const auto t0 = Clock::now();
    const auto finish = [&] {
        result.wallSeconds = since(t0);
        return result;
    };

    World world = forgeWorld(seed, data, dataDir, params);
    if (!world.error.empty()) {
        result.worldError = world.error;
        return finish();
    }
    ForgedGame& game = *world.game;
    auto rig = std::make_unique<Rig>(game, data, params);
    Runner run{result, rig.get()};

    // Walk: the walk bot from the spawn to the quest giver's settlement, as it walks to the nearest settlement in the nightly.
    run.begin(PlaythroughStep::Walk);
    const QuestBinding binding = rig->parley.binding();
    if (!binding.bound) {
        run.fail(PlaythroughStep::Walk, "no quest giver to walk to, the world has no errand: " + binding.reason);
        return finish();
    }
    const NpcSpawn* giver = game.cast ? game.cast->find(binding.giverNpc) : nullptr;
    const PlacedPoi* poi = game.pois ? game.pois->find(binding.dungeonPoi) : nullptr;
    if (giver == nullptr || poi == nullptr) {
        run.fail(PlaythroughStep::Walk, "the errand names a giver or a dungeon the world does not place");
        return finish();
    }
    const Leg toGiver = walkLeg(game, game.spawn.x, game.spawn.z, giver->x, giver->z);
    if (!toGiver.arrived) {
        run.end(PlaythroughStep::Walk, toGiver.stalled ? PlaythroughStatus::Stalled : PlaythroughStatus::Failed, "to the giver: " + toGiver.why + ", " + describeLeg(toGiver));
        return finish();
    }
    rig->ground("giver", giver->x, giver->groundY, giver->z);
    rig->placeAt(giver->x + 1.0, static_cast<double>(giver->groundY) + 0.5, giver->z);
    run.done(PlaythroughStep::Walk, std::format("to {} ({}) at the settlement, {}", binding.giverName, giver->archetype, describeLeg(toGiver)));

    // Talk: F beside the giver opens the conversation.
    run.begin(PlaythroughStep::Talk);
    if (rig->parley.npcInReach(rig->scene) == nullptr) {
        run.fail(PlaythroughStep::Talk, std::format("nobody within {:.0f} m to talk to after standing beside {}", kTalkReachM, binding.giverNpc));
        return finish();
    }
    rig->press(InputFrame::Interact);
    if (!rig->parley.open()) {
        run.fail(PlaythroughStep::Talk, rig->carry.itemWithinReach(rig->scene) ? "F did not open the conversation: an item lies within reach and takes F" : "F did not open the conversation");
        return finish();
    }
    const ParleyView opened = rig->parley.view(rig->scene);
    run.done(PlaythroughStep::Talk, std::format("{} says \"{}\", {} choice(s)", opened.speaker, opened.text.substr(0, 60), opened.choices.size()));

    // Accept: the errand is offered and taken.
    run.begin(PlaythroughStep::Accept);
    const bool offered = std::any_of(opened.choices.begin(), opened.choices.end(), [](const ParleyChoice& c) { return c.key == "accept"; });
    if (!offered) {
        std::string keys;
        for (const ParleyChoice& c : opened.choices) keys += (keys.empty() ? "" : ", ") + c.key;
        run.fail(PlaythroughStep::Accept, "no accept choice, the giver offers: " + keys);
        return finish();
    }
    rig->parley.choose(rig->scene, "accept");
    rig->settle();
    rig->parley.close();
    if (rig->parley.stageOf(rig->scene) != QuestStage::Active) {
        run.fail(PlaythroughStep::Accept, "accepted, but the quest is not active afterwards");
        return finish();
    }
    run.done(PlaythroughStep::Accept, std::format("\"{}\": fetch {} from the {} {}", rig->parley.quest() ? rig->parley.quest()->title : binding.giverName, binding.itemName, binding.dungeonKind, binding.where));

    // Fight: the walk bot to the dungeon, the body in through the entrance, then the nearest room with enemies is cleared by the fight bot.
    run.begin(PlaythroughStep::Fight);
    const Leg toDungeon = walkLeg(game, giver->x, giver->z, poi->x, poi->z);
    if (!toDungeon.arrived) {
        run.end(PlaythroughStep::Fight, toDungeon.stalled ? PlaythroughStatus::Stalled : PlaythroughStatus::Failed, "walk to the dungeon: " + toDungeon.why + ", " + describeLeg(toDungeon));
        return finish();
    }
    rig->ground("dungeon", poi->x, poi->groundY, poi->z);
    rig->placeAt(toDungeon.end.x, static_cast<double>(poi->groundY) + 0.5, toDungeon.end.z);
    if (rig->entry->prompt(rig->scene).empty()) {
        run.fail(PlaythroughStep::Fight, std::format("no entrance prompt {:.1f} m from the dungeon {}", std::hypot(toDungeon.end.x - poi->x, toDungeon.end.z - poi->z), poi->id));
        return finish();
    }
    rig->press(InputFrame::Confirm);
    for (int i = 0; i < 150 && !(rig->entry->inside(rig->scene) && !rig->entry->content().empty()); ++i) rig->tick();
    if (!rig->entry->inside(rig->scene) || rig->entry->content().empty()) {
        run.fail(PlaythroughStep::Fight, "Confirm at the entrance did not put the player inside a baked dungeon");
        return finish();
    }
    for (int i = 0; i < 60; ++i) rig->tick();
    const DungeonContent& content = rig->entry->content();
    const graph::NodeId entryRoom = rig->entryRoom();
    graph::NodeId enemyRoom = 0;
    std::size_t enemyRoomSteps = std::numeric_limits<std::size_t>::max();
    for (const DungeonRoom& room : content.rooms) {
        if (content.enemiesIn(room.id).empty()) continue;
        const std::optional<std::vector<RoomStep>> route = routeBetweenRooms(rig->entry->greyBox(), entryRoom, room.id);
        if (route && route->size() < enemyRoomSteps) {
            enemyRoomSteps = route->size();
            enemyRoom = room.id;
        }
    }
    if (enemyRoom == 0) {
        run.fail(PlaythroughStep::Fight, std::format("the dungeon {} has no room with enemies the body can walk to ({} rooms, {} with enemies)", poi->id, content.rooms.size(), roomsWithEnemies(content)));
        return finish();
    }
    std::string roles;
    for (const std::size_t e : content.enemiesIn(enemyRoom)) roles += std::string(roles.empty() ? "" : " and ") + roleName(content.enemies[e].role);
    int deaths = 0;
    if (!rig->clearRoom(enemyRoom, deaths)) {
        run.giveUp(PlaythroughStep::Fight, std::format("{} rooms in, {} ({}), {} death(s)", enemyRoomSteps, describeRoom(content, enemyRoom), roles, deaths));
        return finish();
    }
    run.done(PlaythroughStep::Fight, std::format("walked {}; cleared {} ({}) {} room(s) in with {} death(s), {} skill, bot seed {}", describeLeg(toDungeon), describeRoom(content, enemyRoom), roles,
        enemyRoomSteps, deaths, params.skill, params.botSeed));

    // Loot: on to the room the errand's item lies in, and F picks it up.
    run.begin(PlaythroughStep::Loot);
    const ItemSite site = itemSite(game, binding.dungeonPoi, binding.itemId);
    if (!site.inside || !site.reachable) {
        run.fail(PlaythroughStep::Loot, site.inside ? std::format("the item lies in {} which the body cannot walk to", describeRoom(content, site.room)) : "the item lies outdoors at the dungeon's mouth, not in a room");
        return finish();
    }
    const auto item = std::find_if(content.loot.begin(), content.loot.end(), [&](const PlacedLoot& l) { return l.itemId == binding.itemId; });
    if (item == content.loot.end()) {
        run.fail(PlaythroughStep::Loot, "the item " + binding.itemId + " is not in the dungeon's content");
        return finish();
    }
    for (;;) {
        if (!rig->walkRooms(site.room, deaths)) {
            run.giveUp(PlaythroughStep::Loot, "to " + describeRoom(content, site.room));
            return finish();
        }
        const Move move = rig->steerTo(item->x, item->z, 0.3);
        if (move == Move::Arrived) break;
        if (move == Move::Stuck) {
            run.giveUp(PlaythroughStep::Loot, "to the item in " + describeRoom(content, site.room));
            return finish();
        }
        if (++deaths > params.maxDeaths) {
            run.fail(PlaythroughStep::Loot, std::format("died {} times on the way to the item", deaths));
            return finish();
        }
    }
    if (!rig->carry.itemWithinReach(rig->scene)) {
        run.fail(PlaythroughStep::Loot, "standing on the item's spot, nothing is within reach");
        return finish();
    }
    rig->press(InputFrame::Interact);
    if (rig->carry.countOf(rig->scene, binding.itemId) != 1) {
        run.fail(PlaythroughStep::Loot, "F did not pick the item up: " + rig->carry.notice(rig->scene));
        return finish();
    }
    if (rig->parley.stageOf(rig->scene) != QuestStage::Found) {
        run.fail(PlaythroughStep::Loot, "the item is carried but the quest did not move to Found");
        return finish();
    }
    run.done(PlaythroughStep::Loot, std::format("picked {} up in {}, quest Found, {} death(s) so far", binding.itemName, describeRoom(content, site.room), deaths));

    // Save: a slot inside the dungeon with the errand half done.
    run.begin(PlaythroughStep::Save);
    const std::filesystem::path dir = params.saveDir.empty() ? std::filesystem::temp_directory_path() / std::format("ichor_playthrough_{}", seed) : params.saveDir;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    SaveSlots slots(dir, SavePolicy{});
    const SaveContext context{seed, 1, data.digest()};
    const std::uint64_t savedHash = rig->scene.stateHash();
    const std::string savedJournal = journalText(rig->parley, rig->scene);
    if (!slots.save(kSlot, rig->scene, context)) {
        run.fail(PlaythroughStep::Save, "the slot was not written: " + slots.lastError());
        return finish();
    }
    run.done(PlaythroughStep::Save, std::format("slot \"{}\" written to {}, state hash {:016x}", kSlot, dir.string(), savedHash));

    // Quit: nothing below the app can be told to quit. The game is torn down instead, which is what a quit leaves behind.
    run.begin(PlaythroughStep::Quit);
    rig.reset();
    run.rig = nullptr;
    run.end(PlaythroughStep::Quit, PlaythroughStatus::Unsupported,
        "no headless quit driver: the app quits through the pause menu in main.cpp (Platform::requestQuit) and no system below it can be told to quit; "
        "the headless game (scene and every system) was torn down in its place");

    // Load: a fresh game, the slot read back, and the scene is where the save left it.
    run.begin(PlaythroughStep::Load);
    auto loaded = std::make_unique<Rig>(game, data, params);
    run.rig = loaded.get();
    loaded->beginStep();
    const LoadResult load = slots.load(kSlot, loaded->scene, context);
    if (load.status != LoadStatus::Loaded) {
        run.fail(PlaythroughStep::Load, std::format("the slot did not load (status {}): {}", static_cast<int>(load.status), slots.lastError()));
        return finish();
    }
    const std::uint64_t loadedHash = loaded->scene.stateHash();
    loaded->ground("giver", giver->x, giver->groundY, giver->z);
    loaded->ground("dungeon", poi->x, poi->groundY, poi->z);
    for (int i = 0; i < 90; ++i) loaded->tick();
    std::string wrong;
    if (loadedHash != savedHash) wrong += std::format("state hash {:016x} differs from the saved {:016x}; ", loadedHash, savedHash);
    if (!loaded->entry->inside(loaded->scene)) wrong += "the player is not inside the dungeon; ";
    if (!loaded->fight->inDungeon()) wrong += "the dungeon fight does not know the player is inside; ";
    if (loaded->parley.stageOf(loaded->scene) != QuestStage::Found) wrong += "the quest is not Found; ";
    if (loaded->carry.countOf(loaded->scene, binding.itemId) != 1) wrong += "the item is not carried; ";
    if (!roomCleared(loaded->scene, loaded->entry->content(), enemyRoom)) wrong += std::format("{} is not cleared; ", describeRoom(loaded->entry->content(), enemyRoom));
    if (journalText(loaded->parley, loaded->scene) != savedJournal) wrong += "the journal differs; ";
    std::filesystem::remove_all(dir, ec);
    if (!wrong.empty()) {
        run.fail(PlaythroughStep::Load, "loaded, but " + wrong);
        return finish();
    }
    run.done(PlaythroughStep::Load, std::format("same state hash, inside the dungeon with {} carried, quest Found, {} still cleared", binding.itemName, describeRoom(loaded->entry->content(), enemyRoom)));
    return finish();
}

}
