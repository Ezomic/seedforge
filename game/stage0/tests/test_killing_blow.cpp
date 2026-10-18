#include "ichor/sim/scene.h"
#include "ichor/stage0/camera_view.h"
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
    Arena()
        : scene(7)
    {
        stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
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
};

// The old stand-in: is the attacker within the victim's forward half plane?
bool facingProxy(float victimYaw, glm::vec3 victimAt, glm::vec3 attackerAt)
{
    return glm::dot(forwardFromYaw(victimYaw), glm::normalize(attackerAt - victimAt)) > -0.2f;
}

constexpr float kPi = 3.14159265f;

}

TEST_CASE("the frustum test and the facing proxy disagree when the player looks away from a foe the camera sees")
{
    const glm::dvec3 player(0.0, 0.0, 3.0);
    const glm::dvec3 foe(0.0, 0.0, -2.0);
    const CameraPose pose = followCamera(player, foe);
    // Player faces +Z, away from the foe: the proxy says off screen, but the camera sits behind the player and sees the foe.
    CHECK_FALSE(facingProxy(kPi, glm::vec3(player), glm::vec3(foe)));
    CHECK(inFrustum(pose, foe, 0.3f));
}

TEST_CASE("the frustum test and the facing proxy disagree for an attacker behind the camera")
{
    const glm::dvec3 player(0.0, 0.0, 3.0);
    const glm::dvec3 foe(0.0, 0.0, -2.0);
    const CameraPose pose = followCamera(player, foe);
    const glm::dvec3 behindCamera(0.0, 0.0, 9.0);
    // Player faces +Z: the proxy counts the attacker as in front, but it is behind the camera.
    CHECK(facingProxy(kPi, glm::vec3(player), glm::vec3(behindCamera)));
    CHECK_FALSE(inFrustum(pose, behindCamera, 0.3f));
}

TEST_CASE("the frustum rejects hostile and out of range points")
{
    const CameraPose pose = followCamera({0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(inFrustum(pose, {nan, 0.0, 0.0}, 0.3f));
    CHECK_FALSE(inFrustum(pose, {0.0, 0.0, -2.0}, std::numeric_limits<float>::infinity()));
    CHECK_FALSE(inFrustum(pose, {0.0, 0.0, -500.0}, 0.3f));
    CHECK_FALSE(inFrustum(pose, {80.0, 0.0, -2.0}, 0.3f));
    CHECK(inFrustum(pose, {0.0, 0.0, -2.0}, 0.3f));
}

TEST_CASE("a killing blow stamps attack kind, recovery, stamina at impact and idle time")
{
    Arena a;
    a.stage.recorder().noteAttack(false, a.scene.time());
    InputFrame block;
    block.buttons = InputFrame::Block;
    a.scene.tick(kDt, block);
    for (int i = 0; i < 90; ++i) {
        a.scene.tick(kDt);
    }
    a.player().blocking = false;
    a.player().state = static_cast<std::uint8_t>(CombatState::Recovery);
    a.player().stamina = 37.0f;
    a.foe().attack = static_cast<std::uint8_t>(AttackKind::Heavy);

    a.stage.receiveHit(a.scene, a.playerEntity(), a.foeEntity(), 1000.0f, 0.0f, true);

    REQUIRE_FALSE(a.player().alive());
    const DeathFacts& facts = a.stage.recorder().lastFacts();
    CHECK(facts.attack == KillingAttack::Heavy);
    CHECK(facts.victimRecovering);
    CHECK(facts.staminaAtImpact == Catch::Approx(37.0f));
    CHECK(facts.secondsSinceInput == Catch::Approx(1.5f).margin(0.1));
    CHECK(a.stage.recorder().lastCause() == DeathCause::WhiffPunished);
    CHECK(a.stage.recorder().lastRow().find(",heavy,1,37.0,1.5") != std::string::npos);
}

TEST_CASE("input resets the idle clock the killing blow reports")
{
    Arena a;
    a.stage.recorder().noteAttack(false, a.scene.time());
    for (int i = 0; i < 120; ++i) {
        a.scene.tick(kDt);
    }
    InputFrame steer;
    steer.moveX = 1.0f;
    a.scene.tick(kDt, steer);
    for (int i = 0; i < 6; ++i) {
        a.scene.tick(kDt);
    }
    a.stage.receiveHit(a.scene, a.playerEntity(), a.foeEntity(), 1000.0f, 0.0f, true);
    CHECK(a.stage.recorder().lastFacts().secondsSinceInput == Catch::Approx(0.1f).margin(0.05));
}
