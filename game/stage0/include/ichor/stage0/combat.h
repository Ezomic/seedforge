#pragma once

#include "ichor/core/reflect.h"
#include "ichor/sim/components.h"
#include "ichor/sim/physics.h"
#include "ichor/sim/scene.h"
#include "ichor/stage0/recorder.h"
#include "ichor/stage0/status_effect.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ichor::stage0 {

// Attacks commit. Once Windup begins there is no cancel: that is the design under test.
enum class CombatState : std::uint8_t { Idle, Windup, Active, Recovery, Dodging, Staggered, Dead };
enum class AttackKind : std::uint8_t { Light, Heavy };
enum class Side : std::uint8_t { Player, Foe };

const char* name(CombatState state);
const char* name(DeathCause cause);

// One fighter. Facing is its own yaw so the tracking clamp is exact.
struct Combatant {
    std::uint8_t side = 0;
    float health = 100.0f;
    float stamina = 100.0f;
    float poise = 100.0f;
    std::uint8_t state = 0;
    std::uint8_t attack = 0;
    float stateTime = 0.0f;
    float sinceStaminaSpend = 999.0f;
    float sincePoiseDamage = 999.0f;
    bool blocking = false;
    bool traceDone = false;
    float yaw = 0.0f; // radians, 0 looks down -Z
    glm::vec3 dodgeDirection{0.0f};
    std::uint8_t deathCause = 0;
    bool retryHeld = false; // edge detection for the retry button, part of the state so replay sees it
    StatusSet status;

    CombatState combatState() const { return static_cast<CombatState>(state); }
    bool alive() const { return combatState() != CombatState::Dead; }
    bool committed() const
    {
        return combatState() == CombatState::Windup || combatState() == CombatState::Active || combatState() == CombatState::Recovery;
    }
};

enum class BrainState : std::uint8_t { Unaware, Approach, Neutral, Tell, Punish, Committing, Rest };
const char* name(BrainState state);

// The skirmisher's decision layer: a rhythm generator, not an opponent.
struct SkirmisherBrain {
    std::uint8_t state = 0;
    float stateTime = 0.0f;
    float desire = 0.0f;
    float pressure = 0.0f;
    std::int32_t commits = 0;
    std::int32_t followUps = 0;
    float strafeSign = 1.0f;
    float nextThreshold = 1.0f;

    BrainState brainState() const { return static_cast<BrainState>(state); }
};

enum class FoeRole : std::uint8_t { Skirmisher, Controller };

enum class ControllerState : std::uint8_t { Unaware, Reposition, Cast };
const char* name(ControllerState state);

// The controller's decision layer. Counts whole ticks, never seconds, so nothing drifts.
struct ControllerBrain {
    std::uint8_t state = 0;
    std::int32_t stateTicks = 0;
    std::int32_t cooldownTicks = 0;
    std::int32_t casts = 0;
    float strafeSign = 1.0f;

    ControllerState controllerState() const { return static_cast<ControllerState>(state); }
};

// Every number from stage0-log.md section 3, plus the six knobs doc 18 names.
struct Tuning {
    float maxHealth = 100.0f;
    float maxStamina = 100.0f;
    float maxPoise = 100.0f;
    float lightDamage = 30.0f;
    float heavyDamage = 50.0f;
    float lightPoiseDamage = 40.0f;
    float heavyPoiseDamage = 70.0f;
    float lightWindup = 0.35f;
    float heavyWindup = 0.7f;
    float lightActive = 0.15f;
    float heavyActive = 0.2f;
    float lightRecovery = 0.4f;
    float heavyRecovery = 0.8f;
    float lightStaminaCost = 20.0f;
    float heavyStaminaCost = 35.0f;
    float dodgeStaminaCost = 25.0f;
    float staminaRegenDelay = 1.0f;
    float staminaRegenPerSec = 25.0f;
    float blockDamageRatio = 0.4f; // the share that still reaches health
    float blockedPoiseRatio = 0.35f;
    float poiseRegenDelay = 1.5f;
    float poiseRegenPerSec = 40.0f;
    float staggerDuration = 0.9f;
    float dodgeDuration = 0.35f;
    float dodgeSpeed = 6.0f;
    float moveSpeed = 4.0f;
    float attackRange = 1.6f; // spear reach from the body centre
    float attackRadius = 0.5f;
    float bodyRadius = 0.3f;
    float trackingCapWindupDegPerSec = 30.0f;
    float trackingCapActiveDegPerSec = 0.0f;

    // Skirmisher
    float preferredSpacing = 2.5f;
    float desireThreshold = 1.0f;
    float desireJitter = 0.15f;
    float desireGainRate = 0.35f;
    float desireGainVsRecovering = 0.6f;
    int restAfterCommits = 3;
    float restAfterSeconds = 6.0f;
    float restDuration = 1.5f;
    float tellDuration = 0.15f;
    float punishReactionTime = 0.25f;
    float foeStaminaCost = 30.0f;
    float foeStaminaRegenPerSec = 15.0f;
    float aggroRange = 12.0f;
    float walkSpeed = 3.5f;
    float strafeSpeedScale = 0.5f;
    float reCommitTolerance = 0.4f;
    float heavyChance = 0.3f;
    int maxFollowUpsIntoStagger = 1;

    // Controller: holds the band between retreat and cast range, slows the target, recasts when the slow runs low.
    float controllerMaxHealth = 40.0f;
    float controllerRetreatBelow = 5.0f;
    float controllerCastRange = 9.0f;
    float controllerStrafeSpeedScale = 0.4f;
    int controllerCastTicks = 45;
    int controllerCooldownTicks = 180;
    int controllerRecastBelowTicks = 60;
    int controllerStrafeFlipTicks = 130;

    // Cues; kNoSound plays nothing.
    std::uint32_t controllerCastCue = kNoSound;
    std::uint32_t lightWindupCue = kNoSound;
    std::uint32_t heavyWindupCue = kNoSound;
    std::uint32_t tellCue = kNoSound;
};

// The experiment: two fighters, the systems that drive them, and the record.
class Stage0 {
public:
    explicit Stage0(Tuning tuning = {});

    // Spawns the arena and both fighters, registers components and systems.
    void install(Scene& scene, glm::dvec3 playerSpawn, glm::dvec3 foeSpawn);

    // Both fighters back to spawn with full bars; an unfinished fight is dropped.
    void resetFight(Scene& scene);

    void debugLines(const Scene& scene, std::vector<LineSegment>& out) const;
    void overlay(const Scene& scene, std::vector<std::string>& lines) const;

    // Which brain the foe gets on the next reset; call before install or resetFight.
    void setFoeRole(FoeRole role) { role_ = role; }
    FoeRole foeRole() const { return role_; }

    const Tuning& tuning() const { return tuning_; }
    FightRecorder& recorder() { return recorder_; }
    StableId playerId() const { return player_; }
    StableId foeId() const { return foe_; }

    // Shared by both systems.
    bool tryAttack(Scene& scene, entt::entity who, AttackKind kind);
    bool tryDodge(Scene& scene, entt::entity who, glm::vec3 direction);
    bool applyStatus(Scene& scene, entt::entity who, StatusId id, std::int32_t durationTicks = 0);
    void receiveHit(Scene& scene, entt::entity victim, entt::entity attacker, float damage, float poiseDamage, bool attackerOnScreen);
    float allowedTurnRate(const Combatant& c) const;

private:
    void combatSystem(Scene& scene, double dt, const InputFrame& input);
    void skirmisherSystem(Scene& scene, double dt);
    void controllerSystem(Scene& scene);
    void enterState(Scene& scene, entt::entity who, Combatant& c, CombatState next);
    void tickStatus(Scene& scene, entt::entity who, Combatant& c);
    struct PendingHit {
        entt::entity victim;
        entt::entity attacker;
        float damage;
        float poiseDamage;
        bool onScreen;
    };
    void attackTrace(Scene& scene, entt::entity attacker, Combatant& c, std::vector<PendingHit>& hits);
    void tickCombatant(Scene& scene, entt::entity entity, entt::entity opponent, float fdt, std::vector<PendingHit>& hits);

    Tuning tuning_;
    FoeRole role_ = FoeRole::Skirmisher;
    FightRecorder recorder_;
    StableId player_{};
    StableId foe_{};
    glm::dvec3 playerSpawn_{0.0};
    glm::dvec3 foeSpawn_{0.0};
};

glm::vec3 forwardFromYaw(float yaw);
float yawTowards(glm::dvec3 from, glm::dvec3 to);
constexpr float kArenaHalf = 10.0f; // the arena is 20 m square, walls at plus and minus this

}

namespace ichor {

template <>
struct Reflect<stage0::ControllerBrain> {
    static constexpr auto fields()
    {
        using B = stage0::ControllerBrain;
        return std::make_tuple(field("state", &B::state), field("stateTicks", &B::stateTicks), field("cooldownTicks", &B::cooldownTicks),
            field("casts", &B::casts), field("strafeSign", &B::strafeSign));
    }
};

template <>
struct Reflect<stage0::Combatant> {
    static constexpr auto fields()
    {
        using C = stage0::Combatant;
        return std::make_tuple(field("side", &C::side), field("health", &C::health), field("stamina", &C::stamina), field("poise", &C::poise),
            field("state", &C::state), field("attack", &C::attack), field("stateTime", &C::stateTime),
            field("sinceStaminaSpend", &C::sinceStaminaSpend), field("sincePoiseDamage", &C::sincePoiseDamage),
            field("blocking", &C::blocking), field("traceDone", &C::traceDone), field("yaw", &C::yaw),
            field("dodgeDirection", &C::dodgeDirection), field("deathCause", &C::deathCause), field("retryHeld", &C::retryHeld), field("status", &C::status));
    }
};

template <>
struct Reflect<stage0::SkirmisherBrain> {
    static constexpr auto fields()
    {
        using B = stage0::SkirmisherBrain;
        return std::make_tuple(field("state", &B::state), field("stateTime", &B::stateTime), field("desire", &B::desire),
            field("pressure", &B::pressure), field("commits", &B::commits), field("followUps", &B::followUps),
            field("strafeSign", &B::strafeSign), field("nextThreshold", &B::nextThreshold));
    }
};

}
