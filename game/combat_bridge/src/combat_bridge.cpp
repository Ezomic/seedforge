#include "ichor/combat_bridge/combat_bridge.h"

#include "ichor/core/log.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

// Spawned a little above the ground so the controller settles onto it.
constexpr glm::dvec3 kSettleLift{0.0, 0.5, 0.0};
constexpr double kCameraGroundClearM = 0.6;
constexpr double kCameraJumpM = 5.0;

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
    stage_.attach(scene, player, start, site_.enemyPost + kSettleLift);
    scene.addSystem("combat_bridge", [this](Scene& s, double dt, const InputFrame&) { step(s, dt); });
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

void CombatBridge::step(Scene& scene, double dt)
{
    auto& registry = scene.registry();
    const entt::entity player = scene.find(player_);
    if (player == entt::null) {
        return;
    }
    if (phase_ == Phase::WaitingForGround) {
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
    pose.position.y = std::max(pose.position.y, static_cast<double>(game_.heightfield.heightAt(pose.position.x, pose.position.z)) + kCameraGroundClearM);
    view.camera.position = pose.position;
    view.camera.rotation = pose.rotation;

    exile_.append(scene, alpha, frameSeconds, static_cast<float>(config_.fixedDt), stage_.tuning(), view);
    if (bullWarden_) {
        stage0::appendBullWarden(scene, stage_.foeId(), alpha, stage_.tuning(), *bullWarden_, view);
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

void CombatBridge::overlay(const Scene& scene, std::vector<std::string>& lines) const
{
    if (live()) {
        stage_.overlay(scene, lines);
    }
}

}
