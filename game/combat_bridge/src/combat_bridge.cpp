#include "ichor/combat_bridge/combat_bridge.h"

#include "ichor/combat_bridge/role_bot.h"
#include "ichor/combat_bridge/role_view.h"
#include "ichor/core/log.h"
#include "ichor/stage0/session_ledger.h"
#include "ichor/stage0/free_look.h"
#include "ichor/world/dungeon_entry.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ichor {

namespace {

// Spawned a little above the ground so the controller settles onto it.
constexpr glm::dvec3 kSettleLift{0.0, 0.5, 0.0};
constexpr double kCameraGroundClearM = 0.6;
constexpr double kCameraJumpM = 5.0;
constexpr double kSightStepM = 1.5;
constexpr int kSightMaxSteps = 200;

// A shot is blocked when the ground rises above the straight line between the two eyes. Trees are not solid in the
// engine yet, so they do not block it either.
bool terrainClear(const Heightfield& heightfield, glm::dvec3 from, glm::dvec3 to)
{
    if (heightfield.empty()) return true;
    const glm::dvec3 span = to - from;
    const double length = glm::length(span);
    if (!std::isfinite(length)) return false;
    const int steps = static_cast<int>(std::clamp(length / kSightStepM, 1.0, static_cast<double>(kSightMaxSteps)));
    for (int i = 1; i < steps; ++i) {
        const glm::dvec3 at = from + span * (static_cast<double>(i) / steps);
        if (static_cast<double>(heightfield.heightAt(at.x, at.z)) > at.y) return false;
    }
    return true;
}

}

CombatBridge::CombatBridge(const ForgedGame& game, const CombatSite& site, CombatBridgeConfig config)
    : game_(game)
    , site_(site)
    , config_(config)
    , stage_(config.tuning)
{
    if (!std::isfinite(config_.respawnDelaySeconds) || config_.respawnDelaySeconds < 0.0) {
        config_.respawnDelaySeconds = CombatBridgeConfig{}.respawnDelaySeconds;
    }
    if (!std::isfinite(config_.fixedDt) || config_.fixedDt <= 0.0) {
        config_.fixedDt = CombatBridgeConfig{}.fixedDt;
    }
    stage_.setFoeRole(config_.role);
    stage_.setAnchorTuning(config_.anchor);
    stage_.setArtilleryTuning(config_.artillery);
    if (!game_.heightfield.empty()) {
        walking_ = std::make_unique<ForgedWalking>(game_.heightfield, game_.waterField.get(), CapabilitySet{});
    }
    // The Anchor and the Artillery walk by the rules the player does: wading slows them and ground too steep stops them.
    // A dungeon is outside the world: its halls are flat, have no water, and the world's ground says nothing about them.
    stage_.setGround({[this](glm::dvec3 from, glm::dvec3 to) { return inDungeonPocket(from) || terrainClear(game_.heightfield, from, to); },
        [this](double x, double z) { return game_.heightfield.empty() ? 0.0 : static_cast<double>(game_.heightfield.heightAt(x, z)); },
        [this](glm::dvec3 at) { return game_.waterField && !inDungeonPocket(at) ? static_cast<double>(wadingSpeedFactor(game_.waterField->at(at.x, at.z).depth)) : 1.0; },
        [this](glm::dvec3 at, glm::dvec3 velocity) { return walking_ && !inDungeonPocket(at) ? walking_->filterVelocity(at, velocity) : velocity; }});
}

void CombatBridge::install(Scene& scene, StableId player, glm::dvec3 start)
{
    if (!site_.found) {
        return;
    }
    player_ = player;
    start_ = start;
    lastSettlement_ = site_.settlement;
    stage_.setLegacyControls(config_.legacyControls);
    openFightLog();
    stage_.attach(scene, player, start, site_.enemyPost + kSettleLift);
    scene.addSystem("combat_bridge", [this](Scene& s, double dt, const InputFrame&) { step(s, dt); });
}

void CombatBridge::openFightLog()
{
    if (config_.fightsCsv.empty()) {
        return;
    }
    stampFightLog(game_.sites[site_.settlement].id);
    auto& recorder = stage_.recorder();
    recorder.open(config_.fightsCsv);
    if (!std::filesystem::exists(config_.fightsCsv)) {
        logf(LogLevel::Warn, "combat: {} could not be created, so no fights are recorded", config_.fightsCsv.string());
    }
    recorder.setSession({std::format("forged-{}", game_.desc.seed), "combat"});
}

void CombatBridge::stampFightLog(std::string_view site)
{
    stage_.recorder().setContext("world_seed,site", std::format("{},{}", game_.desc.seed, stage0::cleanField(site)));
}

void CombatBridge::setFightSite(std::string_view site)
{
    if (!config_.fightsCsv.empty()) {
        stampFightLog(site);
    }
}

glm::dvec3 CombatBridge::respawnFor(std::size_t settlement)
{
    const auto found = respawnPoints_.find(settlement);
    if (found != respawnPoints_.end()) {
        return found->second;
    }
    const glm::dvec3 point = respawnPoint(game_, settlement) + kSettleLift;
    respawnPoints_.emplace(settlement, point);
    return point;
}

void CombatBridge::setSpawnsForLastSettlement()
{
    stage_.setSpawns(respawnFor(lastSettlement_), site_.enemyPost + kSettleLift);
}

void CombatBridge::setFightSpawns(glm::dvec3 playerFeet, glm::dvec3 foeFeet)
{
    stage_.setSpawns(playerFeet + kSettleLift, foeFeet + kSettleLift);
}

// The same reset the stage gives the foe, for the foe alone: resetFight would also move the player.
void CombatBridge::setFoe(Scene& scene, glm::dvec3 feet, bool alive, stage0::FoeRole role)
{
    auto& registry = scene.registry();
    const entt::entity foe = scene.find(stage_.foeId());
    if (foe == entt::null) {
        return;
    }
    const stage0::Tuning& tuning = stage_.tuning();
    stage_.setFoeRole(role);
    stage0::Combatant combatant;
    combatant.side = 1;
    if (alive) {
        combatant.health = tuning.maxHealth;
        combatant.stamina = tuning.maxStamina;
        combatant.poise = tuning.maxPoise;
        if (role == stage0::FoeRole::Anchor) {
            combatant.health = stage_.anchorTuning().maxHealth;
            combatant.poise = stage_.anchorTuning().maxPoise;
        } else if (role == stage0::FoeRole::Artillery) {
            combatant.health = stage_.artilleryTuning().maxHealth;
            combatant.poise = stage_.artilleryTuning().maxPoise;
        }
        const entt::entity player = scene.find(player_);
        combatant.yaw = stage0::yawTowards(feet, player != entt::null ? registry.get<Transform>(player).position : feet);
    } else {
        combatant.health = 0.0f;
        combatant.state = static_cast<std::uint8_t>(stage0::CombatState::Dead);
    }
    registry.emplace_or_replace<stage0::Combatant>(foe, combatant);
    registry.remove<stage0::SkirmisherBrain>(foe);
    registry.remove<stage0::ControllerBrain>(foe);
    registry.remove<stage0::AnchorBrain>(foe);
    registry.remove<stage0::ArtilleryBrain>(foe);
    if (alive && role == stage0::FoeRole::Anchor) {
        registry.emplace<stage0::AnchorBrain>(foe);
    } else if (alive && role == stage0::FoeRole::Artillery) {
        registry.emplace<stage0::ArtilleryBrain>(foe);
    } else if (alive) {
        registry.emplace<stage0::SkirmisherBrain>(foe);
    }
    registry.get<CharacterBody>(foe).intent = glm::vec3(0.0f);
    Transform moved = registry.get<Transform>(foe);
    moved.position = feet + kSettleLift;
    scene.enqueue(SetTransformCommand{stage_.foeId(), moved});
    foeParked_ = !alive;
    lastRoleState_ = 0;
    lastEnemyAlive_ = alive;
    lastEnemyState_ = combatant.state;
}

void CombatBridge::placeFoe(Scene& scene, glm::dvec3 feet, std::optional<stage0::FoeRole> role) { setFoe(scene, feet, true, role.value_or(config_.role)); }

void CombatBridge::parkFoe(Scene& scene, glm::dvec3 feet) { setFoe(scene, feet, false, config_.role); }

void CombatBridge::returnToWorld()
{
    if (site_.found) {
        setFightSite(game_.sites[site_.settlement].id);
    }
    setSpawnsForLastSettlement();
    returningToWorld_ = true;
    returnParked_ = false;
}

void CombatBridge::step(Scene& scene, double dt)
{
    auto& registry = scene.registry();
    const entt::entity player = scene.find(player_);
    if (player == entt::null) {
        return;
    }
    if (phase_ == Phase::WaitingForGround) {
        if (startAt_) {
            stage_.setSpawns(registry.get<Transform>(player).position, *startAt_ + kSettleLift);
            stage_.spawnFoe(scene);
            startAt_.reset();
            phase_ = Phase::FoeQueued;
            return;
        }
        if (!groundReady_ || groundReady_(site_.enemyPost)) {
            stage_.spawnFoe(scene);
            phase_ = Phase::FoeQueued;
        }
        return;
    }
    if (phase_ == Phase::FoeQueued) {
        // The spawn command runs at the start of the next tick.
        if (scene.find(stage_.foeId()) == entt::null) {
            return;
        }
        if (auto* walker = registry.try_get<Player>(player)) {
            walker->metresPerSecond = stage_.tuning().moveSpeed;
        }
        stage_.resetFight(scene);
        setSpawnsForLastSettlement(); // the fight starts where the player is; a death sends them to the settlement
        phase_ = Phase::Live;
        const glm::dvec3 here = registry.get<Transform>(player).position;
        const glm::dvec3 there = registry.get<Transform>(scene.find(stage_.foeId())).position;
        logf(LogLevel::Info, "combat: fight set up at tick {}, player at {:.1f}, {:.1f}, {:.1f}, enemy at {:.1f}, {:.1f}, {:.1f}", scene.tickCount(), here.x, here.y, here.z, there.x, there.y, there.z);
        return;
    }

    const glm::dvec3 at = registry.get<Transform>(player).position;
    if (returningToWorld_ && !inDungeonPocket(at)) {
        // Parked where the player stands, which has ground, until the enemy's own post has some.
        if (!returnParked_) {
            parkFoe(scene, at);
            returnParked_ = true;
        } else if (!groundReady_ || groundReady_(site_.enemyPost)) {
            placeFoe(scene, site_.enemyPost);
            returningToWorld_ = false;
        }
    }
    std::size_t inside = 0;
    if (settlementAt(game_, at.x, at.z, inside) && inside != lastSettlement_) {
        lastSettlement_ = inside;
        setSpawnsForLastSettlement();
    }
    logTransitions(scene);
    if (registry.get<stage0::Combatant>(player).alive()) {
        deadSeconds_ = 0.0;
        return;
    }
    deadSeconds_ += dt;
    if (deadSeconds_ >= config_.respawnDelaySeconds) {
        deadSeconds_ = 0.0;
        respawns_ += 1;
        stage_.resetFight(scene);
        logf(LogLevel::Info, "combat: tick {} the player is back at settlement {}", scene.tickCount(), lastSettlement_);
    }
}

// One line per enemy wind-up and per death, so a run says where in its ticks the interesting moments are.
void CombatBridge::logTransitions(const Scene& scene)
{
    const auto& registry = scene.registry();
    const entt::entity player = scene.find(player_);
    const entt::entity foe = scene.find(stage_.foeId());
    if (player == entt::null || foe == entt::null || !registry.all_of<stage0::Combatant>(player) || !registry.all_of<stage0::Combatant>(foe)) {
        return;
    }
    const auto& fighter = registry.get<stage0::Combatant>(player);
    const auto& enemy = registry.get<stage0::Combatant>(foe);
    if (enemy.state != lastEnemyState_ && enemy.combatState() == stage0::CombatState::Windup) {
        logf(LogLevel::Info, "combat: tick {} the enemy winds up a {} attack", scene.tickCount(), enemy.attack == static_cast<std::uint8_t>(stage0::AttackKind::Heavy) ? "heavy" : "light");
    }
    if (enemy.alive() != lastEnemyAlive_ && !enemy.alive()) {
        logf(LogLevel::Info, "combat: tick {} the enemy is dead", scene.tickCount());
    }
    if (fighter.alive() != lastPlayerAlive_ && !fighter.alive()) {
        logf(LogLevel::Info, "combat: tick {} the player is dead ({})", scene.tickCount(), stage0::name(static_cast<stage0::DeathCause>(fighter.deathCause)));
    }
    if (const auto* anchor = registry.try_get<stage0::AnchorBrain>(foe)) {
        if (anchor->state != lastRoleState_ && anchor->anchorState() == stage0::AnchorState::Windup) {
            logf(LogLevel::Info, "combat: tick {} the anchor plants and winds up a slam", scene.tickCount());
        }
        lastRoleState_ = anchor->state;
    } else if (const auto* artillery = registry.try_get<stage0::ArtilleryBrain>(foe)) {
        if (artillery->state != lastRoleState_ && artillery->artilleryState() == stage0::ArtilleryState::Aim) {
            logf(LogLevel::Info, "combat: tick {} the artillery marks a point at {:.1f}, {:.1f}", scene.tickCount(), artillery->aimX, artillery->aimY);
        }
        lastRoleState_ = artillery->state;
    }
    lastEnemyState_ = enemy.state;
    lastEnemyAlive_ = enemy.alive();
    lastPlayerAlive_ = fighter.alive();
}

void CombatBridge::enableBot(const stage0::BotSkill& skill, std::uint64_t seed)
{
    bot_.emplace(skill, seed);
}

InputFrame CombatBridge::botInput(Scene& scene)
{
    if (!bot_ || !live()) {
        return {};
    }
    if (stage_.recorder().fightsWritten() != botFights_ || respawns_ != botRespawns_) {
        bot_->reset();
        botFights_ = stage_.recorder().fightsWritten();
        botRespawns_ = respawns_;
    }
    // The skirmisher's bot reads the foe's combat state, which the Anchor and the Artillery never change: they have their own.
    const entt::entity foe = scene.find(stage_.foeId());
    if (foe != entt::null && (scene.registry().all_of<stage0::AnchorBrain>(foe) || scene.registry().all_of<stage0::ArtilleryBrain>(foe))) {
        return roleBotInput(scene, stage_);
    }
    return bot_->next(scene, stage_);
}

stage0::FightOutcome CombatBridge::outcome(const Scene& scene) const
{
    if (!live()) {
        return stage0::FightOutcome::Ongoing;
    }
    const auto& registry = scene.registry();
    const entt::entity player = scene.find(stage_.playerId());
    const entt::entity foe = scene.find(stage_.foeId());
    if (player == entt::null || foe == entt::null) {
        return stage0::FightOutcome::Ongoing;
    }
    if (!registry.get<stage0::Combatant>(player).alive()) {
        return stage0::FightOutcome::PlayerDied;
    }
    if (!registry.get<stage0::Combatant>(foe).alive()) {
        return stage0::FightOutcome::FoeDied;
    }
    return stage0::FightOutcome::Ongoing;
}

CombatHudState CombatBridge::hud(const Scene& scene) const
{
    const stage0::Tuning& tuning = stage_.tuning();
    CombatHudState state{tuning.maxHealth, tuning.maxHealth, tuning.maxStamina, tuning.maxStamina};
    if (!live()) {
        return state;
    }
    const entt::entity player = scene.find(stage_.playerId());
    if (const auto* fighter = player != entt::null ? scene.registry().try_get<stage0::Combatant>(player) : nullptr) {
        state.health = fighter->health;
        state.stamina = fighter->stamina;
    }
    return state;
}

std::optional<double> CombatBridge::foeDistance(const Scene& scene) const
{
    if (!live()) {
        return std::nullopt;
    }
    const auto& registry = scene.registry();
    const entt::entity player = scene.find(stage_.playerId());
    const entt::entity foe = scene.find(stage_.foeId());
    if (player == entt::null || foe == entt::null || !registry.all_of<Transform>(player) || !registry.all_of<Transform>(foe)) {
        return std::nullopt;
    }
    const auto* enemy = registry.try_get<stage0::Combatant>(foe);
    if (enemy == nullptr || !enemy->alive()) {
        return std::nullopt;
    }
    const glm::dvec3 from = registry.get<Transform>(player).position;
    const glm::dvec3 to = registry.get<Transform>(foe).position;
    return std::hypot(to.x - from.x, to.z - from.z);
}

bool CombatBridge::loadVisuals(Renderer& renderer, const std::filesystem::path& artDir)
{
    bool loaded = exile_.load(artDir / "exile", renderer);
    if (!loaded) {
        log(LogLevel::Warn, "combat: art/exile did not load, so the player is not drawn (git lfs pull --include=\"art/exile/*\")");
    }
    auto kit = std::make_unique<stage0::BullWardenKit>();
    if (stage0::cookBullWarden(artDir / "monsters" / "bull_warden", *kit)) {
        kit->mesh = renderer.meshCount();
        if (renderer.uploadBundle(kit->bundle)) {
            bullWarden_ = std::move(kit);
        }
    }
    if (!bullWarden_) {
        log(LogLevel::Warn, "combat: art/monsters/bull_warden did not load, so the enemy is not drawn (git lfs pull --include=\"art/monsters/*\")");
        loaded = false;
    }
    return loaded;
}

void CombatBridge::buildView(const Scene& scene, double alpha, RenderView& view, CombatDebugAids aids)
{
    if (!live()) {
        return;
    }
    const auto& registry = scene.registry();
    const entt::entity player = scene.find(stage_.playerId());
    const entt::entity foe = scene.find(stage_.foeId());
    if (player == entt::null || foe == entt::null) {
        return;
    }
    const auto* playerAt = registry.try_get<Transform>(player);
    const auto* playerWas = registry.try_get<PreviousTransform>(player);
    const auto* foeAt = registry.try_get<Transform>(foe);
    const auto* foeWas = registry.try_get<PreviousTransform>(foe);
    const auto* fighter = registry.try_get<stage0::Combatant>(player);
    if (!playerAt || !playerWas || !foeAt || !foeWas || !fighter) {
        return;
    }

    const double now = scene.time();
    const double frameSeconds = lastViewTime_ < 0.0 ? 0.0 : std::clamp(now - lastViewTime_, 0.0, 0.25);
    lastViewTime_ = now;

    // The rig clamps its eye to the arena's walls, so it is run around the player and the result moved back.
    const glm::dvec3 p = glm::mix(playerWas->position, playerAt->position, alpha);
    const glm::dvec3 f = glm::mix(foeWas->position, foeAt->position, alpha);
    // A respawn is a jump, not a walk: the camera starts again behind the player instead of swinging round to them.
    if (glm::length(p - lastViewPlayer_) > kCameraJumpM) {
        rig_ = stage0::CameraRig(cameraTuning());
    }
    lastViewPlayer_ = p;
    const glm::dvec3 origin(p.x, 0.0, p.z);
    rig_.setFraming(stage_.lockedOn() ? stage0::CameraFraming::LockOn : stage0::CameraFraming::Follow);
    stage0::CameraPose pose = stage_.lockedOn() ? rig_.update(p - origin, f - origin, frameSeconds) : rig_.updateFree(p - origin, fighter->yaw, frameSeconds);
    pose.position += origin;
    if (!inDungeonPocket(pose.position)) {
        pose.position.y = std::max(pose.position.y, static_cast<double>(game_.heightfield.heightAt(pose.position.x, pose.position.z)) + kCameraGroundClearM);
    }
    view.camera.position = pose.position;
    view.camera.rotation = pose.rotation;

    exile_.append(scene, alpha, frameSeconds, static_cast<float>(config_.fixedDt), stage_.tuning(), view);
    if (bullWarden_ && !foeParked_) {
        appendRoleFoe(scene, stage_, alpha, *bullWarden_, view);
    }
    if (foeParked_) {
        return;
    }
    std::vector<LineSegment> lines;
    if (const auto* foeFighter = registry.try_get<stage0::Combatant>(foe)) {
        appendEnemyTelegraph(enemyTelegraph(*foeFighter, stage_.tuning()), f, stage0::forwardFromYaw(foeFighter->yaw), stage_.tuning(),
            [this](double x, double z) { return static_cast<double>(game_.heightfield.heightAt(x, z)); }, lines);
    }
    if (aids.stageLines) {
        stage_.debugLines(scene, lines);
    }
    for (const LineSegment& line : lines) {
        view.lines.push_back({line.a, line.b, line.color});
    }
}

float CombatBridge::moveYaw(const Scene& scene) const
{
    const entt::entity player = scene.find(stage_.playerId());
    const entt::entity foe = scene.find(stage_.foeId());
    if (!live() || player == entt::null || foe == entt::null) {
        return 0.0f;
    }
    const auto& registry = scene.registry();
    const auto* fighter = registry.try_get<stage0::Combatant>(player);
    if (fighter == nullptr) {
        return 0.0f;
    }
    const float toFoe = stage0::yawTowards(glm::dvec3(0.0), registry.get<Transform>(foe).position - registry.get<Transform>(player).position);
    return stage0::viewYaw(stage_.lockedOn(), fighter->yaw, toFoe);
}

void CombatBridge::overlay(const Scene& scene, std::vector<std::string>& lines) const
{
    if (live()) {
        stage_.overlay(scene, lines);
    }
}

}
