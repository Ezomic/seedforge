#include "ichor/combat_bridge/dungeon_fight.h"

#include "ichor/core/log.h"
#include "ichor/graph/mission.h"
#include "ichor/stage0/bull_warden.h"

#include <cmath>
#include <format>

namespace ichor {

namespace {

// The entry room's far corner: clear of the arrival point, the exit marker and every doorway.
constexpr glm::dvec3 kParkingOffset{-4.0, 0.0, -4.0};

stage0::FoeRole foeRoleOf(DungeonEnemyRole role)
{
    switch (role) {
    case DungeonEnemyRole::Anchor: return stage0::FoeRole::Anchor;
    case DungeonEnemyRole::Artillery: return stage0::FoeRole::Artillery;
    case DungeonEnemyRole::Skirmisher: break;
    }
    return stage0::FoeRole::Skirmisher;
}

}

DungeonFight::DungeonFight(CombatBridge& bridge, const DungeonEntry& entry, DungeonFightConfig config)
    : bridge_(bridge)
    , entry_(entry)
    , config_(config)
{
    if (!std::isfinite(config_.wakeDelaySeconds) || config_.wakeDelaySeconds < 0.0) {
        config_.wakeDelaySeconds = DungeonFightConfig{}.wakeDelaySeconds;
    }
}

void DungeonFight::install(Scene& scene, StableId player)
{
    player_ = player;
    scene.addSystem("dungeon_fight", [this](Scene& s, double, const InputFrame&) { step(s); });
}

glm::dvec3 DungeonFight::entryCentre() const
{
    const DungeonContent& content = entry_.content();
    for (const DungeonRoom& room : content.rooms) {
        if (room.type == graph::mission::kEntry) {
            return room.centre;
        }
    }
    return content.rooms.front().centre;
}

glm::dvec3 DungeonFight::parkingSpot() const { return entryCentre() + kParkingOffset; }

const DungeonEnemy* DungeonFight::find(const Key& key) const
{
    for (const DungeonEnemy& enemy : entry_.content().enemies) {
        if (enemy.room == key.room && enemy.slot == key.slot) {
            return &enemy;
        }
    }
    return nullptr;
}

std::optional<std::size_t> DungeonFight::engaged() const
{
    if (!engaged_) {
        return std::nullopt;
    }
    const std::vector<DungeonEnemy>& enemies = entry_.content().enemies;
    for (std::size_t i = 0; i < enemies.size(); ++i) {
        if (enemies[i].room == engaged_->room && enemies[i].slot == engaged_->slot) {
            return i;
        }
    }
    return std::nullopt;
}

void DungeonFight::arrive(Scene& scene)
{
    here_ = true;
    poi_ = entry_.content().poiId;
    bridge_.setFightSite(poi_);
    engaged_.reset();
    wakeAt_ = scene.time();
    bridge_.parkFoe(scene, parkingSpot());
    bridge_.setFightSpawns(entryCentre(), parkingSpot());
    const DungeonContent& content = entry_.content();
    std::string rooms;
    for (std::size_t i = 0; i < content.rooms.size(); ++i) {
        if (const std::size_t count = content.enemiesIn(content.rooms[i].id).size(); count != 0) {
            rooms += std::format(" #{} {} x{}", i, content.rooms[i].type, count);
        }
    }
    logf(LogLevel::Info, "dungeon fight: {} has enemies in {} rooms ({} already cleared):{}", content.poiId, roomsWithEnemies(content), clearedRooms(scene, content), rooms);
}

void DungeonFight::leave()
{
    here_ = false;
    engaged_.reset();
    bridge_.returnToWorld();
}

void DungeonFight::engage(Scene& scene, const DungeonEnemy& enemy)
{
    bridge_.placeFoe(scene, enemy.post, foeRoleOf(enemy.role));
    bridge_.setFightSpawns(entryCentre(), enemy.post);
    engaged_ = Key{enemy.room, enemy.slot};
}

void DungeonFight::standDown(Scene& scene)
{
    bridge_.parkFoe(scene, parkingSpot());
    bridge_.setFightSpawns(entryCentre(), parkingSpot());
    engaged_.reset();
}

void DungeonFight::step(Scene& scene)
{
    const DungeonContent& content = entry_.content();
    if (!entry_.inside(scene) || content.empty()) {
        if (here_) {
            leave();
        }
        return;
    }
    if (bridge_.waiting()) {
        bridge_.startAt(parkingSpot());
        return;
    }
    if (!bridge_.live()) {
        return;
    }
    // A load can swap one dungeon for another without a step outside.
    if (here_ && content.poiId != poi_) {
        here_ = false;
    }
    if (!here_) {
        arrive(scene);
    }

    auto& registry = scene.registry();
    const entt::entity player = scene.find(player_);
    const entt::entity foe = scene.find(bridge_.stage().foeId());
    if (player == entt::null || foe == entt::null) {
        return;
    }
    const auto* fighter = registry.try_get<stage0::Combatant>(player);
    const auto* enemyState = registry.try_get<stage0::Combatant>(foe);
    if (fighter == nullptr || enemyState == nullptr) {
        return;
    }
    const DungeonRoom* room = content.roomAt(registry.get<Transform>(player).position);

    if (engaged_) {
        const DungeonEnemy* enemy = find(*engaged_);
        if (enemy == nullptr) {
            engaged_.reset();
        } else if (!enemyState->alive()) {
            recordKill(scene, content.poiId, *enemy);
            engaged_.reset();
            wakeAt_ = scene.time() + config_.wakeDelaySeconds;
            logf(LogLevel::Info, "dungeon fight: tick {} enemy {} of room {} is dead", scene.tickCount(), enemy->slot, enemy->room);
            return;
        } else if (room == nullptr || room->id != enemy->room) {
            standDown(scene);
        }
    }
    // A reset after the player's death revives the foe where it was sent: it belongs to nobody then.
    if (!engaged_ && enemyState->alive()) {
        standDown(scene);
    }

    if (engaged_ || !fighter->alive() || room == nullptr || scene.time() < wakeAt_) {
        return;
    }
    for (const DungeonEnemy& enemy : content.enemies) {
        if (enemy.room == room->id && !enemyKilled(scene, content.poiId, enemy.room, enemy.slot)) {
            engage(scene, enemy);
            return;
        }
    }
}

void DungeonFight::appendView(const Scene& scene, RenderView& view) const
{
    const stage0::BullWardenKit* kit = bridge_.bullWarden();
    if (!here_ || kit == nullptr) {
        return;
    }
    const DungeonContent& content = entry_.content();
    for (const DungeonEnemy& enemy : content.enemies) {
        if (enemyKilled(scene, content.poiId, enemy.room, enemy.slot) || (engaged_ && engaged_->room == enemy.room && engaged_->slot == enemy.slot)) {
            continue;
        }
        const DungeonRoom* room = content.room(enemy.room);
        stage0::Combatant resting;
        resting.side = 1;
        resting.yaw = stage0::yawTowards(enemy.post, room != nullptr ? room->centre : enemy.post);
        stage0::appendBullWardenAt(resting, enemy.post, false, scene.time(), bridge_.stage().tuning(), *kit, view);
    }
}

void DungeonFight::overlay(const Scene& scene, std::vector<std::string>& lines) const
{
    if (!here_) {
        return;
    }
    const DungeonContent& content = entry_.content();
    lines.push_back(std::format("DUNGEON {}  rooms cleared {}/{}  {}", content.poiId, clearedRooms(scene, content), roomsWithEnemies(content), engaged_ ? "fighting" : "no one engaged"));
}

}
