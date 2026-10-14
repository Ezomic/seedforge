#include "ichor/stage0/combat.h"

#include "ichor/core/dmath.h"
#include "ichor/core/log.h"

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

float wrapAngle(float a)
{
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
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

void Stage0::install(Scene& scene, glm::dvec3 playerSpawn, glm::dvec3 foeSpawn)
{
    scene.registerComponent<Combatant>("stage0.combatant");
    scene.registerComponent<SkirmisherBrain>("stage0.skirmisher");
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

    SpawnCommand foe;
    foe.id = foe_;
    foe.name.value = "skirmisher";
    foe.transform.position = foeSpawn;
    foe.character = CharacterBody{tuning_.bodyRadius, 0.6f};
    scene.enqueue(foe);

    scene.addSystem("stage0.combat", [this](Scene& s, double dt, const InputFrame& input) { combatSystem(s, dt, input); });
    scene.addSystem("stage0.skirmisher", [this](Scene& s, double dt, const InputFrame&) { skirmisherSystem(s, dt); });
}

void Stage0::resetFight(Scene& scene)
{
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
            scene.registry().emplace_or_replace<SkirmisherBrain>(entity, SkirmisherBrain{});
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
    c.deathCause = static_cast<std::uint8_t>(DeathCause::Bleed);
    enterState(scene, who, c, CombatState::Dead);
    const Combatant& playerC = scene.registry().get<Combatant>(scene.find(player_));
    const Combatant& foeC = scene.registry().get<Combatant>(scene.find(foe_));
    recorder_.noteDeath(c.side == 0, DeathCause::Bleed, playerC.health, foeC.health, scene.time());
}

void Stage0::receiveHit(Scene& scene, entt::entity victim, entt::entity attacker, float damage, float poiseDamage, bool attackerOnScreen)
{
    Combatant& c = scene.registry().get<Combatant>(victim);
    const Combatant& a = scene.registry().get<Combatant>(attacker);
    if (!c.alive()) {
        return;
    }
    const bool blocked = c.blocking && c.combatState() == CombatState::Idle;
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
        const bool againstWall = std::max(std::abs(at.x), std::abs(at.z)) > kArenaHalf - tuning_.bodyRadius - 0.6;
        DeathCause cause = DeathCause::Unreadable;
        if (!attackerOnScreen) cause = DeathCause::OffScreen;
        else if (c.combatState() == CombatState::Recovery) cause = DeathCause::WhiffPunished;
        else if (blocked) cause = DeathCause::MistimedBlock;
        else if (c.stamina <= 1.0f) cause = DeathCause::OutOfStamina;
        else if (againstWall) cause = DeathCause::Cornered;
        c.deathCause = static_cast<std::uint8_t>(cause);
        enterState(scene, victim, c, CombatState::Dead);
        const Combatant& playerC = scene.registry().get<Combatant>(scene.find(player_));
        const Combatant& foeC = scene.registry().get<Combatant>(scene.find(foe_));
        recorder_.noteDeath(c.side == 0, cause, playerC.health, foeC.health, scene.time());
        return;
    }
    if (c.poise <= 0.0f && c.combatState() != CombatState::Staggered) {
        enterState(scene, victim, c, CombatState::Staggered);
    }
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
        // Was the attacker in front of the victim? A forward-facing test stands in for the camera.
        const glm::vec3 toAttacker = glm::normalize(glm::vec3(origin - otherT.position));
        const bool onScreen = glm::dot(forwardFromYaw(otherC.yaw), toAttacker) > -0.2f;
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

    // Retry lives in the input stream, so a recorded session replays its restarts too.
    {
        Combatant& c = scene.registry().get<Combatant>(playerEntity);
        const bool retry = (input.buttons & InputFrame::Confirm) != 0;
        const bool pressed = retry && !c.retryHeld;
        c.retryHeld = retry;
        if (pressed) {
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
            c.poise = std::min(tuning_.maxPoise, c.poise + tuning_.poiseRegenPerSec * fdt);
        }

        // Face the opponent, as fast as the state allows. This clamp replaces i-frames.
        const float wanted = yawTowards(transform.position, registry.get<Transform>(opponent).position);
        const float maxStep = allowedTurnRate(c) * (kPi / 180.0f) * fdt;
        c.yaw = wrapAngle(c.yaw + std::clamp(wrapAngle(wanted - c.yaw), -maxStep, maxStep));

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
                c.poise = tuning_.maxPoise;
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
    for (auto [entity, transform, c] : scene.registry().view<const Transform, const Combatant>().each()) {
        const glm::dvec3 base = transform.position + glm::dvec3(0.0, 0.9, 0.0);
        const glm::vec3 forward = forwardFromYaw(c.yaw);
        out.push_back({base, base + glm::dvec3(forward) * 0.6, glm::vec3(0.9f)});
        const bool heavy = c.attack == static_cast<std::uint8_t>(AttackKind::Heavy);
        if (c.combatState() == CombatState::Windup) {
            // The charge reads as the reach growing out to where the blow will land.
            const float t = std::clamp(c.stateTime / (heavy ? tuning_.heavyWindup : tuning_.lightWindup), 0.0f, 1.0f);
            const glm::vec3 color = heavy ? glm::vec3(1.0f, 0.35f, 0.0f) : glm::vec3(1.0f, 0.82f, 0.25f);
            out.push_back({base, base + glm::dvec3(forward) * static_cast<double>(tuning_.attackRange * t), color});
            ring(base + glm::dvec3(forward) * static_cast<double>(tuning_.attackRange * t), tuning_.attackRadius * (0.4 + 0.6 * t), color, 12);
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
        // Cyan is where it wants to stand, red is where it can hurt you.
        ring(centre, tuning_.preferredSpacing, glm::vec3(0.2f, 0.9f, 0.9f), 32);
        ring(centre, tuning_.attackRange + tuning_.attackRadius + tuning_.bodyRadius, glm::vec3(0.6f, 0.15f, 0.15f), 32);
    }
}

void Stage0::overlay(const Scene& scene, std::vector<std::string>& lines) const
{
    for (auto [entity, c] : scene.registry().view<const Combatant>().each()) {
        lines.push_back(std::format("{:<10} {:<10} HP {:5.1f}  ST {:5.1f}  PO {:5.1f}  t {:4.2f}{}",
            c.side == 0 ? "PLAYER" : "SKIRM", name(c.combatState()), c.health, c.stamina, c.poise, c.stateTime, c.blocking ? "  [block]" : ""));
        if (const auto* brain = scene.registry().try_get<SkirmisherBrain>(entity)) {
            lines.push_back(std::format("           {:<10} desire {:4.2f}/{:4.2f}  pressure {:4.1f}s  commits {}",
                name(brain->brainState()), brain->desire, brain->nextThreshold, brain->pressure, brain->commits));
        }
    }
    lines.push_back(std::format("fights {}  {}", recorder_.fightsWritten(), recorder_.running() ? "[fight on]" : "J light  K heavy  L block  Space dodge  Enter retry"));
}

}
