#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;
using namespace ichor::stage0;

namespace {

constexpr double kDt = 1.0 / 60.0;

struct Arena {
    Scene scene;
    Stage0 stage;
    explicit Arena(std::uint64_t seed = 1)
        : scene(seed)
    {
        stage.setFoeRole(FoeRole::Controller);
        stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -8.0});
        scene.tick(kDt);
        stage.resetFight(scene);
        for (int i = 0; i < 30; ++i) {
            scene.tick(kDt);
        }
    }
    entt::entity playerEntity() { return scene.find(stage.playerId()); }
    entt::entity foeEntity() { return scene.find(stage.foeId()); }
    Combatant& player() { return scene.registry().get<Combatant>(playerEntity()); }
    Combatant& foe() { return scene.registry().get<Combatant>(foeEntity()); }
    ControllerBrain& brain() { return scene.registry().get<ControllerBrain>(foeEntity()); }
    Transform& playerTransform() { return scene.registry().get<Transform>(playerEntity()); }
    Transform& foeTransform() { return scene.registry().get<Transform>(foeEntity()); }
    double distance() { return glm::length(glm::dvec3(playerTransform().position - foeTransform().position)); }
    bool slowed() { return player().status.find(StatusId::Slow) != nullptr; }

    bool tickUntil(ControllerState state, int limit)
    {
        for (int i = 0; i < limit; ++i) {
            scene.tick(kDt);
            if (brain().controllerState() == state) return true;
        }
        return false;
    }
};

}

TEST_CASE("the controller role carries its own brain, not the skirmisher's")
{
    Arena a;
    REQUIRE(a.scene.registry().all_of<ControllerBrain>(a.foeEntity()));
    REQUIRE_FALSE(a.scene.registry().all_of<SkirmisherBrain>(a.foeEntity()));
    REQUIRE(a.foe().health == Catch::Approx(a.stage.tuning().controllerMaxHealth));
}

TEST_CASE("the controller closes to casting range, then holds its distance")
{
    Arena a;
    const Tuning& t = a.stage.tuning();
    for (int i = 0; i < 60 * 20; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE(a.distance() <= t.controllerCastRange + 1.0f);
    REQUIRE(a.distance() >= t.controllerRetreatBelow - 0.5f);
}

TEST_CASE("it backs away from a player who closes in")
{
    Arena a;
    for (int i = 0; i < 60 * 6; ++i) {
        a.scene.tick(kDt);
    }
    const double before = a.distance();
    a.scene.enqueue(MoveCommand{a.stage.playerId(), glm::normalize(glm::dvec3(a.foeTransform().position - a.playerTransform().position)) * (before - 2.0)});
    a.scene.tick(kDt);
    REQUIRE(a.distance() < a.stage.tuning().controllerRetreatBelow);
    const double close = a.distance();
    for (int i = 0; i < 40; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE(a.distance() > close);
}

TEST_CASE("a cast takes exactly its cast ticks, then the target is slowed")
{
    Arena a;
    REQUIRE(a.tickUntil(ControllerState::Cast, 60 * 10));
    REQUIRE_FALSE(a.slowed());
    int castTicks = 0;
    while (a.brain().controllerState() == ControllerState::Cast && castTicks < 1000) {
        a.scene.tick(kDt);
        ++castTicks;
    }
    REQUIRE(castTicks == a.stage.tuning().controllerCastTicks);
    REQUIRE(a.slowed());
    REQUIRE(a.stage.recorder().foe().attacks == 1);
    REQUIRE(a.stage.recorder().foe().hits == 1);
    REQUIRE(a.stage.recorder().foe().whiffs == 0);
}

TEST_CASE("the controller stands still while it casts")
{
    Arena a;
    REQUIRE(a.tickUntil(ControllerState::Cast, 60 * 10));
    const glm::dvec3 at = a.foeTransform().position;
    for (int i = 0; i < a.stage.tuning().controllerCastTicks - 1; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE(glm::length(a.foeTransform().position - at) < 1e-3);
}

TEST_CASE("it recasts on the cooldown and not while the slow still has time on it")
{
    Arena a;
    for (int i = 0; i < 60 * 40; ++i) {
        a.scene.tick(kDt);
    }
    const int casts = a.stage.recorder().foe().attacks;
    const int cycle = a.stage.tuning().controllerCastTicks + a.stage.tuning().controllerCooldownTicks;
    REQUIRE(casts >= (60 * 40 - 200) / cycle - 1);
    REQUIRE(casts <= (60 * 40) / cycle + 1);
    REQUIRE(a.brain().casts == casts);
}

TEST_CASE("a target that is already slowed is not cast on")
{
    Arena a;
    REQUIRE(a.player().status.apply(StatusId::Slow, 60 * 60));
    for (int i = 0; i < 60 * 15; ++i) {
        a.scene.tick(kDt);
        a.player().status.apply(StatusId::Slow, 60 * 60);
    }
    REQUIRE(a.stage.recorder().foe().attacks == 0);
    REQUIRE(a.brain().casts == 0);
}

TEST_CASE("a target that dies mid-cast ends the cast and the aggression")
{
    Arena a;
    REQUIRE(a.tickUntil(ControllerState::Cast, 60 * 10));
    for (int i = 0; i < 10; ++i) {
        a.scene.tick(kDt);
    }
    a.player().state = static_cast<std::uint8_t>(CombatState::Dead);
    a.scene.tick(kDt);
    REQUIRE(a.brain().controllerState() == ControllerState::Unaware);
    REQUIRE_FALSE(a.slowed());
    for (int i = 0; i < 120; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE_FALSE(a.slowed());
    REQUIRE(a.brain().controllerState() == ControllerState::Unaware);
}

TEST_CASE("staggering the controller interrupts the cast: the slow never lands")
{
    Arena a;
    REQUIRE(a.tickUntil(ControllerState::Cast, 60 * 10));
    for (int i = 0; i < 10; ++i) {
        a.scene.tick(kDt);
    }
    a.foe().state = static_cast<std::uint8_t>(CombatState::Staggered);
    a.foe().stateTime = 0.0f;
    a.scene.tick(kDt);
    REQUIRE(a.brain().controllerState() != ControllerState::Cast);
    for (int i = 0; i < 60; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE_FALSE(a.slowed());
    REQUIRE(a.brain().cooldownTicks > 0);
}

TEST_CASE("a target that has left range by the end of the cast is a whiff")
{
    Arena a;
    REQUIRE(a.tickUntil(ControllerState::Cast, 60 * 10));
    a.scene.enqueue(MoveCommand{a.stage.playerId(), glm::dvec3(0.0, 0.0, 6.0)});
    a.scene.tick(kDt);
    REQUIRE(a.distance() > a.stage.tuning().controllerCastRange + 1.0f);
    for (int i = 0; i < a.stage.tuning().controllerCastTicks; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE_FALSE(a.slowed());
    REQUIRE(a.stage.recorder().foe().whiffs == 1);
    REQUIRE(a.stage.recorder().foe().hits == 0);
}

TEST_CASE("a controller with no health left never casts")
{
    Arena a;
    a.foe().health = 0.0f;
    for (int i = 0; i < 60 * 15; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE_FALSE(a.slowed());
    REQUIRE(a.stage.recorder().foe().attacks == 0);
    REQUIRE(a.brain().casts == 0);
}

TEST_CASE("a dead controller stops moving and casting")
{
    Arena a;
    REQUIRE(a.tickUntil(ControllerState::Cast, 60 * 10));
    a.foe().state = static_cast<std::uint8_t>(CombatState::Dead);
    const glm::dvec3 at = a.foeTransform().position;
    for (int i = 0; i < 120; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE_FALSE(a.slowed());
    REQUIRE(glm::length(a.foeTransform().position - at) < 1e-3);
}

TEST_CASE("standing on the target gives a finite position, not a NaN")
{
    Arena a;
    a.scene.registry().get<Transform>(a.foeEntity()).position = a.playerTransform().position;
    for (int i = 0; i < 120; ++i) {
        a.scene.tick(kDt);
    }
    const glm::dvec3 p = a.foeTransform().position;
    REQUIRE(std::isfinite(p.x));
    REQUIRE(std::isfinite(p.y));
    REQUIRE(std::isfinite(p.z));
}

TEST_CASE("a NaN or infinite target position changes nothing and moves nobody")
{
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        Arena a;
        const ControllerBrain before = a.brain();
        a.playerTransform().position.x = bad;
        a.scene.tick(kDt);
        REQUIRE(a.brain().state == before.state);
        REQUIRE(a.brain().casts == before.casts);
        const glm::vec3 intent = a.scene.registry().get<CharacterBody>(a.foeEntity()).intent;
        REQUIRE(glm::length(intent) == 0.0f);
    }
}

TEST_CASE("many ticks do not drift: the same seed gives the same hash, the cast count stays on schedule")
{
    const auto run = [](std::uint64_t seed) {
        Arena a(seed);
        for (int i = 0; i < 60 * 120; ++i) {
            a.scene.tick(kDt);
        }
        REQUIRE(a.distance() <= a.stage.tuning().controllerCastRange + 1.0f);
        REQUIRE(a.distance() >= a.stage.tuning().controllerRetreatBelow - 0.5f);
        REQUIRE(a.brain().stateTicks < 1000);
        return std::make_pair(a.scene.stateHash(), a.brain().casts);
    };
    const auto first = run(7);
    const auto second = run(7);
    REQUIRE(first == second);
    const int cycle = Tuning{}.controllerCastTicks + Tuning{}.controllerCooldownTicks;
    REQUIRE(first.second >= 60 * 120 / cycle - 2);
    REQUIRE(first.second <= 60 * 120 / cycle + 1);
}

TEST_CASE("a retry puts the controller back at full health in its first state")
{
    Arena a;
    REQUIRE(a.tickUntil(ControllerState::Cast, 60 * 10));
    a.foe().health = 3.0f;
    a.stage.resetFight(a.scene);
    REQUIRE(a.brain().controllerState() == ControllerState::Unaware);
    REQUIRE(a.brain().casts == 0);
    REQUIRE(a.foe().health == Catch::Approx(a.stage.tuning().controllerMaxHealth));
}
