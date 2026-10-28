#include "ichor/stage0/combat.h"
#include "ichor/stage0/run_prompt.h"

#include "ichor/stage0/ai_knobs.h"

#include "ichor/core/dmath.h"
#include "ichor/core/log.h"
#include "ichor/stage0/camera_view.h"
#include "ichor/stage0/free_look.h"
#include "ichor/stage0/telegraph_geometry.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Timers are compared with half a tick of slack so 24 steps of 1/60 reach 0.4.
bool elapsed(float stateTime, float duration, float dt)
{
    return stateTime + 0.5f * dt >= duration;
}

}

const char* name(CombatState state)
{
    switch (state) {
    case CombatState::Idle: return "Idle";
    case CombatState::Windup: return "Windup";
    case CombatState::Active: return "Active";
    case CombatState::Recovery: return "Recovery";
    case CombatState::Dodging: return "Dodging";
    case CombatState::Staggered: return "Staggered";
    case CombatState::Dead: return "Dead";
    }
    return "?";
}

const char* name(DeathCause cause)
{
    switch (cause) {
    case DeathCause::OutOfStamina: return "stamina";
    case DeathCause::WhiffPunished: return "whiff_punished";
    case DeathCause::Cornered: return "cornered";
    case DeathCause::MistimedBlock: return "mistimed_block";
    case DeathCause::OffScreen: return "offscreen";
    case DeathCause::Unreadable: return "unreadable";
    case DeathCause::Bleed: return "bleed";
    default: return "unknown";
    }
}

glm::vec3 forwardFromYaw(float yaw)
{
    return {dmath::sin(yaw), 0.0f, -dmath::cos(yaw)};
}

float yawTowards(glm::dvec3 from, glm::dvec3 to)
{
    // Yaw is hashed, so this goes through the deterministic atan2, not libm.
    const glm::vec3 d(to - from);
    return dmath::atan2(d.x, -d.z);
}

Stage0::Stage0(Tuning tuning)
    : tuning_(tuning)
{
}

void Stage0::registerComponents(Scene& scene)
{
    scene.registerComponent<Combatant>("stage0.combatant");
    scene.registerComponent<SkirmisherBrain>("stage0.skirmisher");
    scene.registerComponent<SwarmBrain>("stage0.swarm");
    scene.registerComponent<AnchorBrain>("stage0.anchor");
    scene.registerComponent<ArtilleryBrain>("stage0.artillery");
    scene.registerComponent<ControllerBrain>("stage0.controller");
}

void Stage0::addSystems(Scene& scene)
{
    scene.addSystem("stage0.combat", [this](Scene& s, double dt, const InputFrame& input) { combatSystem(s, dt, input); });
    scene.addSystem("stage0.skirmisher", [this](Scene& s, double dt, const InputFrame&) { skirmisherSystem(s, dt); });
    scene.addSystem("stage0.controller", [this](Scene& s, double, const InputFrame&) { controllerSystem(s); });
    scene.addSystem("stage0.anchor", [this](Scene& s, double, const InputFrame&) { anchorSystem(s); });
    scene.addSystem("stage0.artillery", [this](Scene& s, double, const InputFrame&) { artillerySystem(s); });
}

float Stage0::poiseCap(const entt::registry& registry, entt::entity who) const
{
    if (registry.all_of<AnchorBrain>(who)) return anchorTuning_.maxPoise;
    if (registry.all_of<ArtilleryBrain>(who)) return artilleryTuning_.maxPoise;
    return tuning_.maxPoise;
}

void Stage0::spawnFoe(Scene& scene)
{
    SpawnCommand foe;
    foe.id = foe_;
    foe.name.value = role_ == FoeRole::Anchor ? "anchor" : role_ == FoeRole::Artillery ? "artillery" : "skirmisher";
    foe.transform.position = foeSpawn_;
    foe.character = CharacterBody{tuning_.bodyRadius, 0.6f};
    scene.enqueue(foe);
}

void Stage0::attach(Scene& scene, StableId player, glm::dvec3 playerSpawn, glm::dvec3 foeSpawn)
{
    registerComponents(scene);
    arena_ = false;
    playerSpawn_ = playerSpawn;
    foeSpawn_ = foeSpawn;
    player_ = player;
    foe_ = StableId::of("stage0.foe");
    addSystems(scene);
}

void Stage0::install(Scene& scene, glm::dvec3 playerSpawn, glm::dvec3 foeSpawn)
{
    registerComponents(scene);
    playerSpawn_ = playerSpawn;
    foeSpawn_ = foeSpawn;
    player_ = StableId::of("stage0.player");
    foe_ = StableId::of("stage0.foe");

    const auto box = [&](std::string_view name, glm::dvec3 position, glm::vec3 half) {
        SpawnCommand spawn;
        spawn.id = StableId::of(name);
        spawn.name.value = std::string(name);
        spawn.transform.position = position;
        spawn.staticBox = StaticBox{half};
        scene.enqueue(spawn);
    };
    box("floor", {0.0, -0.5, 0.0}, {10.0f, 0.5f, 10.0f});
    box("wall-east", {10.25, 1.0, 0.0}, {0.25f, 1.5f, 10.0f});
    box("wall-west", {-10.25, 1.0, 0.0}, {0.25f, 1.5f, 10.0f});
    box("wall-north", {0.0, 1.0, -10.25}, {10.0f, 1.5f, 0.25f});
    box("wall-south", {0.0, 1.0, 10.25}, {10.0f, 1.5f, 0.25f});

    SpawnCommand player;
    player.id = player_;
    player.name.value = "player";
    player.transform.position = playerSpawn;
    player.player = Player{tuning_.moveSpeed};
    player.character = CharacterBody{tuning_.bodyRadius, 0.6f};
    scene.enqueue(player);

    spawnFoe(scene);

    addSystems(scene);
}

void Stage0::resetFight(Scene& scene)
{
    sinceInput_ = 0.0f;
    recorder_.noteRetry(scene.time());
    for (auto [entity, id, transform] : scene.registry().view<const StableId, Transform>().each()) {
        const bool isPlayer = id == player_;
        const bool isFoe = id == foe_;
        if (!isPlayer && !isFoe) {
            continue;
        }
        Combatant c;
        c.side = isPlayer ? 0 : 1;
        // The button that caused this reset is still down; forgetting that would reset again next tick.
        if (const auto* previous = scene.registry().try_get<Combatant>(entity)) {
            c.retryHeld = previous->retryHeld;
        }
        c.health = tuning_.maxHealth;
        c.stamina = tuning_.maxStamina;
        c.poise = tuning_.maxPoise;
        c.yaw = yawTowards(isPlayer ? playerSpawn_ : foeSpawn_, isPlayer ? foeSpawn_ : playerSpawn_);
        scene.registry().emplace_or_replace<Combatant>(entity, c);
        if (isFoe) {
            auto& registry = scene.registry();
            registry.remove<SkirmisherBrain>(entity);
            registry.remove<ControllerBrain>(entity);
            registry.remove<AnchorBrain>(entity);
            registry.remove<ArtilleryBrain>(entity);
            if (role_ == FoeRole::Controller) {
                registry.get<Combatant>(entity).health = tuning_.controllerMaxHealth;
                registry.emplace<ControllerBrain>(entity, ControllerBrain{});
            } else if (role_ == FoeRole::Anchor) {
                registry.get<Combatant>(entity).health = anchorTuning_.maxHealth;
                registry.get<Combatant>(entity).poise = anchorTuning_.maxPoise;
                registry.emplace<AnchorBrain>(entity, AnchorBrain{});
            } else if (role_ == FoeRole::Artillery) {
                registry.get<Combatant>(entity).health = artilleryTuning_.maxHealth;
                registry.get<Combatant>(entity).poise = artilleryTuning_.maxPoise;
                registry.emplace<ArtilleryBrain>(entity, ArtilleryBrain{});
            } else {
                registry.emplace<SkirmisherBrain>(entity, SkirmisherBrain{});
            }
        }
        scene.enqueue(MoveCommand{id, (isPlayer ? playerSpawn_ : foeSpawn_) - transform.position});
    }
}

float Stage0::allowedTurnRate(const Combatant& c) const
{
    switch (c.combatState()) {
    case CombatState::Windup: return tuning_.trackingCapWindupDegPerSec;
    case CombatState::Active: return tuning_.trackingCapActiveDegPerSec;
    case CombatState::Recovery:
    case CombatState::Staggered:
    case CombatState::Dead: return 0.0f;
    default: return 720.0f;
    }
}

void Stage0::enterState(Scene& scene, entt::entity who, Combatant& c, CombatState next)
{
    c.state = static_cast<std::uint8_t>(next);
    c.stateTime = 0.0f;
    if (next == CombatState::Staggered) {
        c.poise = 0.0f;
        recorder_.noteStagger(c.side == 0);
    }
    if (next == CombatState::Windup) {
        // The cue fires with the wind-up, not with the swing.
        const bool heavy = c.attack == static_cast<std::uint8_t>(AttackKind::Heavy);
        const std::uint32_t cue = heavy ? tuning_.heavyWindupCue : tuning_.lightWindupCue;
        if (cue != kNoSound) {
            scene.emitSound({scene.registry().get<StableId>(who), cue, scene.registry().get<Transform>(who).position});
        }
    }
}

bool Stage0::tryAttack(Scene& scene, entt::entity who, AttackKind kind)
{
    Combatant& c = scene.registry().get<Combatant>(who);
    if (c.combatState() != CombatState::Idle) {
        return false; // committed states refuse input: no cancels, no buffering
    }
    const float cost = kind == AttackKind::Light ? tuning_.lightStaminaCost : tuning_.heavyStaminaCost;
    if (c.side == 1 ? c.stamina < tuning_.foeStaminaCost : c.stamina < cost) {
        return false;
    }
    c.stamina -= c.side == 1 ? tuning_.foeStaminaCost : cost;
    c.sinceStaminaSpend = 0.0f;
    c.attack = static_cast<std::uint8_t>(kind);
    c.traceDone = false;
    c.blocking = false;
    recorder_.noteAttack(c.side == 0, scene.time());
    enterState(scene, who, c, CombatState::Windup);
    return true;
}

bool Stage0::tryDodge(Scene& scene, entt::entity who, glm::vec3 direction)
{
    Combatant& c = scene.registry().get<Combatant>(who);
    if (c.combatState() != CombatState::Idle || c.stamina < tuning_.dodgeStaminaCost) {
        return false;
    }
    c.stamina -= tuning_.dodgeStaminaCost;
    c.sinceStaminaSpend = 0.0f;
    c.blocking = false;
    // A movement burst, not a phase-through: no invincibility anywhere.
    glm::vec3 dir(direction.x, 0.0f, direction.z);
    if (glm::dot(dir, dir) < 1e-6f) {
        dir = -forwardFromYaw(c.yaw);
    }
    c.dodgeDirection = glm::normalize(dir);
    recorder_.noteDodge(c.side == 0);
    enterState(scene, who, c, CombatState::Dodging);
    return true;
}

bool Stage0::applyStatus(Scene& scene, entt::entity who, StatusId id, std::int32_t durationTicks)
{
    Combatant& c = scene.registry().get<Combatant>(who);
    return c.alive() && c.status.apply(id, durationTicks);
}

void Stage0::tickStatus(Scene& scene, entt::entity who, Combatant& c)
{
    const std::int64_t milli = c.status.tick();
    if (milli <= 0) {
        return;
    }
    c.health = std::max(0.0f, c.health - static_cast<float>(milli) / 1000.0f);
    if (c.health > 0.0f) {
        return;
    }
    DeathFacts facts;
    facts.victimRecovering = c.combatState() == CombatState::Recovery;
    facts.outOfStamina = c.stamina <= 1.0f;
    facts.staminaAtImpact = c.stamina;
    facts.secondsSinceInput = sinceInput_;
    c.deathCause = static_cast<std::uint8_t>(DeathCause::Bleed);
    enterState(scene, who, c, CombatState::Dead);
    const Combatant& playerC = scene.registry().get<Combatant>(scene.find(player_));
    const Combatant& foeC = scene.registry().get<Combatant>(scene.find(foe_));
    recorder_.noteDeath(c.side == 0, DeathCause::Bleed, playerC.health, foeC.health, scene.time(), facts);
}

void Stage0::parry(Scene& scene, entt::entity attacker, const WeaponProfile& weapon)
{
    Combatant& a = scene.registry().get<Combatant>(attacker);
    if (!a.alive()) {
        return;
    }
    a.poise = std::max(0.0f, a.poise - weapon.parryPoiseDamage);
    a.sincePoiseDamage = 0.0f;
    if (a.poise <= 0.0f && a.combatState() != CombatState::Staggered) {
        enterState(scene, attacker, a, CombatState::Staggered);
    }
}

void Stage0::receiveHit(Scene& scene, entt::entity victim, entt::entity attacker, float damage, float poiseDamage, bool attackerOnScreen)
{
    Combatant& c = scene.registry().get<Combatant>(victim);
    const Combatant& a = scene.registry().get<Combatant>(attacker);
    if (!c.alive()) {
        return;
    }
    const float staminaAtImpact = c.stamina;
    const bool guarded = c.blocking && c.combatState() == CombatState::Idle;
    const WeaponProfile& weapon = weapons_[c.side];
    if (guarded && weapon.parries() && c.blockTicks >= 1 && c.blockTicks <= weapon.parryWindowTicks) {
        parry(scene, attacker, weapon);
        return;
    }
    const bool blocked = guarded;
    float applied = damage;
    if (blocked) {
        // Block converts damage into stamina drain rather than negating it.
        c.stamina = std::max(0.0f, c.stamina - damage * (1.0f - tuning_.blockDamageRatio) * 2.0f);
        c.sinceStaminaSpend = 0.0f;
        applied = damage * tuning_.blockDamageRatio;
        if (c.stamina <= 0.0f) {
            enterState(scene, victim, c, CombatState::Staggered);
        }
    }
    applied = std::min(applied, c.health); // overkill is not damage dealt
    c.health = std::max(0.0f, c.health - applied);
    const float appliedPoise = blocked ? poiseDamage * tuning_.blockedPoiseRatio : poiseDamage;
    if (appliedPoise > 0.0f) {
        c.poise = std::max(0.0f, c.poise - appliedPoise);
        c.sincePoiseDamage = 0.0f;
    }
    recorder_.noteHit(a.side == 0, applied, blocked);

    if (c.health <= 0.0f) {
        const glm::dvec3 at = scene.registry().get<Transform>(victim).position;
        const bool againstWall = arena_ && std::max(std::abs(at.x), std::abs(at.z)) > kArenaHalf - tuning_.bodyRadius - 0.6;
        DeathFacts facts;
        facts.attackerOnScreen = attackerOnScreen;
        facts.victimRecovering = c.combatState() == CombatState::Recovery;
        facts.blocked = blocked;
        facts.outOfStamina = c.stamina <= 1.0f;
        facts.againstWall = againstWall;
        facts.attack = a.attack == static_cast<std::uint8_t>(AttackKind::Heavy) ? KillingAttack::Heavy : KillingAttack::Light;
        facts.staminaAtImpact = staminaAtImpact;
        facts.secondsSinceInput = sinceInput_;
        const DeathCause cause = classifyDeath(facts);
        c.deathCause = static_cast<std::uint8_t>(cause);
        enterState(scene, victim, c, CombatState::Dead);
        const Combatant& playerC = scene.registry().get<Combatant>(scene.find(player_));
        const Combatant& foeC = scene.registry().get<Combatant>(scene.find(foe_));
        recorder_.noteDeath(c.side == 0, cause, playerC.health, foeC.health, scene.time(), facts);
        return;
    }
    if (c.poise <= 0.0f && c.combatState() != CombatState::Staggered) {
        enterState(scene, victim, c, CombatState::Staggered);
    }
}

bool Stage0::attackerInView(const Scene& scene, glm::dvec3 attackerAt) const
{
    const entt::entity player = scene.find(player_);
    const entt::entity foe = scene.find(foe_);
    if (player == entt::null || foe == entt::null) {
        return true;
    }
    return inFrustum(viewPose(scene, player, foe), attackerAt, tuning_.bodyRadius);
}

CameraPose Stage0::viewPose(const Scene& scene, entt::entity player, entt::entity foe) const
{
    const auto& registry = scene.registry();
    const glm::dvec3 at = registry.get<Transform>(player).position;
    // The framing clamps the eye to the arena's walls, so outside the arena it is worked out around the player and moved back.
    const glm::dvec3 origin = arena_ ? glm::dvec3(0.0) : glm::dvec3(at.x, 0.0, at.z);
    CameraPose pose = lockedOn() ? followCamera(at - origin, registry.get<Transform>(foe).position - origin)
                                 : freeLookCamera(at - origin, registry.get<Combatant>(player).yaw);
    pose.position += origin;
    return pose;
}

void Stage0::attackTrace(Scene& scene, entt::entity attacker, Combatant& c, std::vector<PendingHit>& hits)
{
    const bool heavy = c.attack == static_cast<std::uint8_t>(AttackKind::Heavy);
    const float damage = heavy ? tuning_.heavyDamage : tuning_.lightDamage;
    const float poiseDamage = heavy ? tuning_.heavyPoiseDamage : tuning_.lightPoiseDamage;
    const glm::dvec3 origin = scene.registry().get<Transform>(attacker).position;
    const glm::vec3 forward = forwardFromYaw(c.yaw);

    bool struck = false;
    for (auto [other, otherC, otherT] : scene.registry().view<Combatant, const Transform>().each()) {
        if (other == attacker || !otherC.alive()) {
            continue;
        }
        // Closest point on the swing segment to the target's centre, in the plane.
        const glm::vec3 rel(otherT.position - origin);
        const float along = std::clamp(glm::dot(glm::vec3(rel.x, 0.0f, rel.z), forward), 0.0f, tuning_.attackRange);
        const glm::vec3 closest = forward * along;
        const glm::vec3 gap = glm::vec3(rel.x, 0.0f, rel.z) - closest;
        if (glm::dot(gap, gap) > (tuning_.attackRadius + tuning_.bodyRadius) * (tuning_.attackRadius + tuning_.bodyRadius)) {
            continue;
        }
        struck = true;
        const bool onScreen = attackerInView(scene, origin);
        hits.push_back({other, attacker, damage, poiseDamage, onScreen});
    }
    if (!struck) {
        recorder_.noteWhiff(c.side == 0);
    }
}

void Stage0::combatSystem(Scene& scene, double dt, const InputFrame& input)
{
    const float fdt = static_cast<float>(dt);
    const entt::entity playerEntity = scene.find(player_);
    const entt::entity foeEntity = scene.find(foe_);
    if (playerEntity == entt::null || foeEntity == entt::null || !scene.registry().all_of<Combatant>(playerEntity)) {
        return;
    }
    recorder_.setClock(scene.time());

    const bool steering = std::isfinite(input.moveX) && std::isfinite(input.moveY) && (std::abs(input.moveX) > 0.1f || std::abs(input.moveY) > 0.1f);
    look_ = sanitisedLook(input.lookX);
    sinceInput_ = input.buttons != 0 || steering || look_ != 0.0f ? 0.0f : sinceInput_ + fdt;

    const bool lockPress = (input.buttons & InputFrame::LockOn) != 0;
    if (lockPress && !lockHeld_ && !legacyControls_) lockOn_ = !lockOn_;
    lockHeld_ = lockPress;

    // Retry lives in the input stream, so a recorded session replays its restarts too.
    {
        Combatant& c = scene.registry().get<Combatant>(playerEntity);
        const bool retry = (input.buttons & InputFrame::Confirm) != 0;
        const bool pressed = retry && !c.retryHeld;
        c.retryHeld = retry;
        if (pressed) {
            retryPresses_ += 1;
            resetFight(scene);
            return;
        }
    }

    // Player intent: the committed states refuse everything but time.
    {
        Combatant& c = scene.registry().get<Combatant>(playerEntity);
        if (c.combatState() == CombatState::Idle) {
            // Replay files are data: the stick is sanitised here as applyInput does.
            glm::vec3 stick(std::isfinite(input.moveX) ? input.moveX : 0.0f, 0.0f, std::isfinite(input.moveY) ? -input.moveY : 0.0f);
            if (glm::dot(stick, stick) > 1.0f) stick = glm::normalize(stick);
            if (!legacyControls_) {
                const glm::dvec3 toFoe = scene.registry().get<Transform>(foeEntity).position - scene.registry().get<Transform>(playerEntity).position;
                const float yawToFoe = yawTowards(glm::dvec3(0.0), toFoe);
                const float yaw = viewYaw(lockOn_, c.yaw, yawToFoe);
                stick = cameraRelativeIntent(stick, yaw);
                auto& body = scene.registry().get<CharacterBody>(playerEntity);
                body.intent = cameraRelativeIntent(body.intent, yaw);
            }
            c.blocking = (input.buttons & InputFrame::Block) != 0;
            if ((input.buttons & InputFrame::Attack) != 0) tryAttack(scene, playerEntity, AttackKind::Light);
            else if ((input.buttons & InputFrame::Heavy) != 0) tryAttack(scene, playerEntity, AttackKind::Heavy);
            else if ((input.buttons & InputFrame::Dodge) != 0) tryDodge(scene, playerEntity, stick);
        }
    }

    // Player first, then the foe, whatever order the registry holds them in, and
    // hits land after both have moved so a simultaneous trade stays a trade.
    std::vector<PendingHit> hits;
    tickCombatant(scene, playerEntity, foeEntity, fdt, hits);
    tickCombatant(scene, foeEntity, playerEntity, fdt, hits);
    for (const PendingHit& hit : hits) {
        receiveHit(scene, hit.victim, hit.attacker, hit.damage, hit.poiseDamage, hit.onScreen);
    }
}

void Stage0::tickCombatant(Scene& scene, entt::entity entity, entt::entity opponent, float fdt, std::vector<PendingHit>& hits)
{
    auto& registry = scene.registry();
    const Transform& transform = registry.get<Transform>(entity);
    Combatant& c = registry.get<Combatant>(entity);
    CharacterBody& body = registry.get<CharacterBody>(entity);
    {
        if (!c.alive()) {
            body.intent = glm::vec3(0.0f);
            return;
        }
        c.stateTime += fdt;
        c.sinceStaminaSpend += fdt;
        c.sincePoiseDamage += fdt;
        c.blockTicks = c.blocking && c.combatState() == CombatState::Idle ? c.blockTicks + 1 : 0;

        tickStatus(scene, entity, c);
        if (!c.alive()) {
            body.intent = glm::vec3(0.0f);
            return;
        }
        const float moveScale = c.status.moveScale();

        if (c.sinceStaminaSpend >= tuning_.staminaRegenDelay && !c.blocking) {
            const float regen = c.side == 1 ? tuning_.foeStaminaRegenPerSec : tuning_.staminaRegenPerSec;
            c.stamina = std::min(tuning_.maxStamina, c.stamina + regen * fdt);
        }
        if (c.combatState() == CombatState::Idle && c.sincePoiseDamage >= tuning_.poiseRegenDelay) {
            c.poise = std::min(poiseCap(registry, entity), c.poise + tuning_.poiseRegenPerSec * fdt);
        }

        // Face the opponent, as fast as the state allows. This clamp replaces i-frames.
        const float wanted = yawTowards(transform.position, registry.get<Transform>(opponent).position);
        if (c.side == 0 && !legacyControls_) {
            c.yaw = nextPlayerYaw({c.yaw, lockOn_, look_, wanted, allowedTurnRate(c), fdt});
        } else {
            const float maxStep = allowedTurnRate(c) * (kPi / 180.0f) * fdt;
            c.yaw = wrapAngle(c.yaw + std::clamp(wrapAngle(wanted - c.yaw), -maxStep, maxStep));
        }

        const bool heavy = c.attack == static_cast<std::uint8_t>(AttackKind::Heavy);
        switch (c.combatState()) {
        case CombatState::Windup:
            body.intent = glm::vec3(0.0f);
            if (elapsed(c.stateTime, heavy ? tuning_.heavyWindup : tuning_.lightWindup, fdt)) enterState(scene, entity, c, CombatState::Active);
            break;
        case CombatState::Active:
            body.intent = glm::vec3(0.0f);
            if (!c.traceDone) {
                c.traceDone = true;
                attackTrace(scene, entity, c, hits);
            }
            if (elapsed(c.stateTime, heavy ? tuning_.heavyActive : tuning_.lightActive, fdt)) enterState(scene, entity, c, CombatState::Recovery);
            break;
        case CombatState::Recovery:
            body.intent = glm::vec3(0.0f);
            if (elapsed(c.stateTime, heavy ? tuning_.heavyRecovery : tuning_.lightRecovery, fdt)) enterState(scene, entity, c, CombatState::Idle);
            break;
        case CombatState::Dodging:
            body.intent = c.dodgeDirection * tuning_.dodgeSpeed * moveScale;
            if (elapsed(c.stateTime, tuning_.dodgeDuration, fdt)) enterState(scene, entity, c, CombatState::Idle);
            break;
        case CombatState::Staggered:
            body.intent = glm::vec3(0.0f);
            if (elapsed(c.stateTime, tuning_.staggerDuration, fdt)) {
                // Full poise back, so nobody is stun-locked in a fight that ends in three hits.
                c.poise = poiseCap(registry, entity);
                c.sincePoiseDamage = 999.0f;
                enterState(scene, entity, c, CombatState::Idle);
            }
            break;
        default:
            if (c.blocking) body.intent *= 0.5f;
            // The foe's intent is rebuilt by the skirmisher after this, which applies the scale itself.
            if (c.side == 0) body.intent *= moveScale;
            break;
        }
    }
}

void Stage0::debugLines(const Scene& scene, std::vector<LineSegment>& out) const
{
    const auto ring = [&](glm::dvec3 centre, double radius, glm::vec3 color, int segments = 24) {
        for (int i = 0; i < segments; ++i) {
            const double a0 = 6.283185307179586 * i / segments;
            const double a1 = 6.283185307179586 * (i + 1) / segments;
            out.push_back({centre + glm::dvec3(std::cos(a0) * radius, 0.0, std::sin(a0) * radius), centre + glm::dvec3(std::cos(a1) * radius, 0.0, std::sin(a1) * radius), color});
        }
    };
    const entt::entity playerEntity = scene.find(player_);
    const entt::entity foeAtStart = scene.find(foe_);
    const bool haveCamera = playerEntity != entt::null && foeAtStart != entt::null;
    const CameraPose camera = haveCamera ? viewPose(scene, playerEntity, foeAtStart) : CameraPose{};
    const CameraPose* foeCamera = haveCamera ? &camera : nullptr;
    for (auto [entity, transform, c] : scene.registry().view<const Transform, const Combatant>().each()) {
        const glm::dvec3 base = transform.position + glm::dvec3(0.0, 0.9, 0.0);
        const glm::vec3 forward = forwardFromYaw(c.yaw);
        out.push_back({base, base + glm::dvec3(forward) * 0.6, glm::vec3(0.9f)});
        const bool heavy = c.attack == static_cast<std::uint8_t>(AttackKind::Heavy);
        if (c.combatState() == CombatState::Windup) {
            // The charge reads as the reach growing out to where the blow will land.
            const float t = std::clamp(c.stateTime / (heavy ? tuning_.heavyWindup : tuning_.lightWindup), 0.0f, 1.0f);
            // Light and heavy share a colour: the table's shape and pattern carry the difference.
            appendWindupTelegraph(heavy, {base, forward, tuning_.attackRange, tuning_.attackRadius, t, glm::vec3(1.0f, 0.7f, 0.15f)}, c.side == 1 ? foeCamera : nullptr, tuning_.bodyRadius, out);
        } else if (c.combatState() == CombatState::Active) {
            ring(base + glm::dvec3(forward) * static_cast<double>(tuning_.attackRange), tuning_.attackRadius, glm::vec3(1.0f, 0.1f, 0.1f), 12);
        }
        if (c.blocking) {
            ring(base, 0.45, glm::vec3(0.3f, 0.8f, 1.0f), 12);
        }
    }
    const entt::entity foeEntity = scene.find(foe_);
    if (foeEntity != entt::null) {
        const glm::dvec3 centre = scene.registry().get<Transform>(foeEntity).position + glm::dvec3(0.0, 0.05, 0.0);
        const auto& registry = scene.registry();
        if (const auto* brain = registry.try_get<AnchorBrain>(foeEntity)) {
            // The slam covers the outer ring; the inner one grows to meet it as the wind-up runs out.
            const glm::vec3 amber(1.0f, 0.6f, 0.1f);
            ring(centre, anchorTuning_.slamRadius, brain->anchorState() == AnchorState::Windup ? amber : amber * 0.35f, 40);
            if (brain->anchorState() == AnchorState::Windup) {
                const double t = std::clamp(static_cast<double>(brain->stateTicks) / std::max(anchorTuning_.windupTicks, 1), 0.0, 1.0);
                ring(centre, anchorTuning_.slamRadius * t, glm::vec3(1.0f, 0.2f, 0.1f), 40);
            }
        } else if (const auto* brain = registry.try_get<ArtilleryBrain>(foeEntity)) {
            if (brain->artilleryState() == ArtilleryState::Aim) {
                const double x = brain->aimX;
                const double z = brain->aimY;
                const glm::dvec3 mark(x, groundHeight(x, z, centre.y) + 0.1, z);
                const double t = std::clamp(static_cast<double>(brain->stateTicks) / std::max(artilleryTuning_.windupTicks, 1), 0.0, 1.0);
                ring(mark, artilleryTuning_.blastRadius, glm::vec3(1.0f, 0.15f, 0.15f), 32);
                ring(mark, artilleryTuning_.blastRadius * t, glm::vec3(1.0f, 0.7f, 0.2f), 32);
                out.push_back({mark, mark + glm::dvec3(0.0, 12.0, 0.0), glm::vec3(1.0f, 0.3f, 0.2f)});
            }
        } else {
            // Cyan is where it wants to stand, red is where it can hurt you.
            ring(centre, tuning_.preferredSpacing, glm::vec3(0.2f, 0.9f, 0.9f), 32);
            ring(centre, tuning_.attackRange + tuning_.attackRadius + tuning_.bodyRadius, glm::vec3(0.6f, 0.15f, 0.15f), 32);
        }
    }
}

std::optional<DeathCause> Stage0::playerDeath(const Scene& scene) const
{
    const entt::entity player = scene.find(player_);
    const auto* fighter = player != entt::null ? scene.registry().try_get<Combatant>(player) : nullptr;
    if (fighter == nullptr || fighter->alive()) {
        return std::nullopt;
    }
    return static_cast<DeathCause>(fighter->deathCause);
}

void Stage0::overlay(const Scene& scene, std::vector<std::string>& lines) const
{
    for (auto [entity, c] : scene.registry().view<const Combatant>().each()) {
        const char* label = "SKIRM";
        if (c.side == 0) label = "PLAYER";
        else if (scene.registry().all_of<ControllerBrain>(entity)) label = "CTRL";
        else if (scene.registry().all_of<AnchorBrain>(entity)) label = "ANCHOR";
        else if (scene.registry().all_of<ArtilleryBrain>(entity)) label = "ARTY";
        lines.push_back(std::format("{:<10} {:<10} HP {:5.1f}  ST {:5.1f}  PO {:5.1f}  t {:4.2f}{}",
            label, name(c.combatState()), c.health, c.stamina, c.poise, c.stateTime, c.blocking ? "  [block]" : ""));
        if (const auto* brain = scene.registry().try_get<AnchorBrain>(entity)) {
            lines.push_back(std::format("           {:<10} cooldown {}t  slams {}", name(brain->anchorState()), brain->cooldownTicks, brain->slams));
        }
        if (const auto* brain = scene.registry().try_get<ArtilleryBrain>(entity)) {
            lines.push_back(std::format("           {:<10} cooldown {}t  shots {}", name(brain->artilleryState()), brain->cooldownTicks, brain->shots));
        }
        if (const auto* brain = scene.registry().try_get<ControllerBrain>(entity)) {
            lines.push_back(std::format("           {:<10} cooldown {}t  casts {}", name(brain->controllerState()), brain->cooldownTicks, brain->casts));
        }
        if (const auto* brain = scene.registry().try_get<SkirmisherBrain>(entity)) {
            const entt::entity playerEntity = scene.find(player_);
            OverlayInput input{&c, brain, 0.0f, 0.0f, allowedTurnRate(c)};
            if (const auto* playerTransform = playerEntity != entt::null ? scene.registry().try_get<Transform>(playerEntity) : nullptr) {
                const glm::dvec3 here = scene.registry().get<Transform>(entity).position;
                input.distance = static_cast<float>(glm::length(playerTransform->position - here));
                input.trackingAngleDeg = std::abs(wrapAngle(yawTowards(here, playerTransform->position) - c.yaw)) * (180.0f / kPi);
            }
            for (const OverlayLine& line : overlayLines(input, tuning_)) {
                lines.push_back(std::format("           {:<9} {}", line.label, line.value));
            }
        }
    }
    PromptInput prompt{FightOutcome::Ongoing, DeathCause::Unknown, recorder_.fightsWritten()};
    const entt::entity playerEntity = scene.find(player_);
    const entt::entity foeEntity = scene.find(foe_);
    if (const auto* p = playerEntity != entt::null ? scene.registry().try_get<Combatant>(playerEntity) : nullptr; p && !p->alive()) {
        prompt.outcome = FightOutcome::PlayerDied;
        prompt.cause = static_cast<DeathCause>(p->deathCause);
    } else if (const auto* f = foeEntity != entt::null ? scene.registry().try_get<Combatant>(foeEntity) : nullptr; f && !f->alive()) {
        prompt.outcome = FightOutcome::FoeDied;
    }
    for (const std::string& line : promptLines(prompt)) {
        lines.push_back(line);
    }
}

}
