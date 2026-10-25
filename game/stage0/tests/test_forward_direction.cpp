#include "ichor/sim/scene.h"
#include "ichor/stage0/camera_rig.h"
#include "ichor/stage0/combat.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ichor;
using namespace ichor::stage0;

namespace {

constexpr double kDt = 1.0 / 60.0;

struct Walk {
    Scene scene{7};
    Stage0 stage;
    explicit Walk(bool lockOn, glm::dvec3 player = {0.0, 0.0, 3.0}, glm::dvec3 foe = {0.0, 0.0, -2.0})
    {
        stage.install(scene, player, foe);
        scene.tick(kDt);
        stage.resetFight(scene);
        stage.setLockOn(lockOn);
        for (int i = 0; i < 30; ++i) scene.tick(kDt);
    }
    glm::dvec3 playerAt() const { return scene.registry().get<Transform>(scene.find(stage.playerId())).position; }
    glm::dvec3 foeAt() const { return scene.registry().get<Transform>(scene.find(stage.foeId())).position; }
    float yaw() const { return scene.registry().get<Combatant>(scene.find(stage.playerId())).yaw; }

    // The camera main.cpp builds for the same state, as a horizontal unit view direction and its right-hand side.
    CameraPose camera() const
    {
        CameraRig rig;
        return stage.lockedOn() ? rig.update(playerAt(), foeAt(), kDt) : rig.updateFree(playerAt(), yaw(), kDt);
    }
    void turn(float look, int ticks)
    {
        InputFrame frame;
        frame.lookX = look;
        for (int i = 0; i < ticks; ++i) scene.tick(kDt, frame);
    }
    glm::dvec3 displacementUnder(InputFrame frame, int ticks = 12)
    {
        const glm::dvec3 before = playerAt();
        for (int i = 0; i < ticks; ++i) scene.tick(kDt, frame);
        return playerAt() - before;
    }
};

glm::dvec2 horizontal(glm::dvec3 v)
{
    const double length = std::hypot(v.x, v.z);
    return length > 1e-9 ? glm::dvec2(v.x, v.z) / length : glm::dvec2(0.0);
}

glm::dvec2 viewForward(const CameraPose& pose)
{
    const glm::vec3 f = pose.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    return horizontal(glm::dvec3(f));
}

glm::dvec2 viewRight(const CameraPose& pose)
{
    const glm::vec3 r = pose.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    return horizontal(glm::dvec3(r));
}

void checkKeysAgainstCamera(Walk& walk)
{
    const CameraPose pose = walk.camera();
    const glm::dvec2 ahead = viewForward(pose);
    const glm::dvec2 right = viewRight(pose);
    const glm::dvec3 start = walk.playerAt();
    const glm::dvec3 foe = walk.foeAt();

    InputFrame w;
    w.moveY = 1.0f;
    const glm::dvec2 forward = horizontal(walk.displacementUnder(w));
    CHECK(glm::dot(forward, ahead) > 0.9);

    InputFrame s;
    s.moveY = -1.0f;
    const glm::dvec2 backward = horizontal(walk.displacementUnder(s));
    CHECK(glm::dot(backward, ahead) < -0.9);

    InputFrame d;
    d.moveX = 1.0f;
    const glm::dvec2 strafe = horizontal(walk.displacementUnder(d));
    CHECK(glm::dot(strafe, right) > 0.9);

    // The camera sits behind the player, so forward also moves away from it.
    const glm::dvec2 awayFromEye = horizontal(glm::dvec3(start.x - pose.position.x, 0.0, start.z - pose.position.z));
    CHECK(glm::dot(forward, awayFromEye) > 0.7);
    (void)foe;
}

}

TEST_CASE("W walks away from the camera and where it looks, in free look at several yaws", "[freelook][fight][direction]")
{
    for (const float look : {0.0f, 0.3f, 0.6f, 1.0f, -0.4f, -1.0f}) {
        for (const int ticks : {0, 15, 40, 90}) {
            Walk walk(false);
            walk.turn(look, ticks);
            INFO("look " << look << " for " << ticks << " ticks, yaw " << walk.yaw());
            checkKeysAgainstCamera(walk);
        }
    }
}

TEST_CASE("W walks away from the camera and toward the foe under lock-on, from several places", "[freelook][fight][direction]")
{
    for (const glm::dvec3 player : {glm::dvec3(0.0, 0.0, 3.0), glm::dvec3(4.0, 0.0, 2.0), glm::dvec3(-5.0, 0.0, -4.0), glm::dvec3(3.0, 0.0, -6.0)}) {
        for (const glm::dvec3 foe : {glm::dvec3(0.0, 0.0, -2.0), glm::dvec3(-3.0, 0.0, 1.0), glm::dvec3(5.0, 0.0, 5.0)}) {
            Walk walk(true, player, foe);
            INFO("player " << player.x << "," << player.z << " foe " << foe.x << "," << foe.z);
            checkKeysAgainstCamera(walk);
            const glm::dvec2 toFoe = horizontal(walk.foeAt() - walk.playerAt());
            InputFrame w;
            w.moveY = 1.0f;
            Walk fresh(true, player, foe);
            CHECK(glm::dot(horizontal(fresh.displacementUnder(w)), toFoe) > 0.9);
        }
    }
}

TEST_CASE("the walking guard in a fight blocks where the player goes, not where the keys point", "[freelook][fight][direction]")
{
    // Nothing may move toward +Z (south), as a cliff or deep water there would.
    const auto southWall = [](const glm::dvec3&, const glm::dvec3& velocity) {
        return velocity.z > 0.0 ? glm::dvec3(velocity.x, velocity.y, 0.0) : velocity;
    };
    Walk walk(false, {0.0, 0.0, 0.0}, {0.0, 0.0, -8.0});
    walk.scene.setVelocityFilter(southWall);
    for (int i = 0; i < 200 && std::abs(walk.yaw()) < 2.6f; ++i) walk.turn(1.0f, 1);
    REQUIRE(std::abs(walk.yaw()) >= 2.6f);

    InputFrame w;
    w.moveY = 1.0f;
    CHECK(walk.displacementUnder(w, 20).z < 0.05);

    InputFrame s;
    s.moveY = -1.0f;
    CHECK(walk.displacementUnder(s, 20).z < -0.5);
}
