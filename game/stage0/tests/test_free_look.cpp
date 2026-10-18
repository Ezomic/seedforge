#include "ichor/stage0/camera_rig.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/free_look.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;
using namespace ichor::stage0;
using Catch::Approx;

namespace {

constexpr float kHalfPi = 1.57079632679f;
constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr double kDt = 1.0 / 60.0;
const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

}

TEST_CASE("forward on the stick follows the camera yaw, not the world axes", "[freelook]")
{
    const glm::vec3 forward(0.0f, 0.0f, -4.0f); // what the engine builds for moveY = 1 at speed 4
    const glm::vec3 unrotated = cameraRelativeIntent(forward, 0.0f);
    CHECK(unrotated.x == Approx(0.0f).margin(1e-5f));
    CHECK(unrotated.z == Approx(-4.0f));

    const glm::vec3 east = cameraRelativeIntent(forward, kHalfPi);
    CHECK(east.x == Approx(4.0f).margin(1e-4f));
    CHECK(east.z == Approx(0.0f).margin(1e-4f));

    const glm::vec3 south = cameraRelativeIntent(forward, 2.0f * kHalfPi);
    CHECK(south.z == Approx(4.0f).margin(1e-4f));
}

TEST_CASE("strafe and backwards are relative to the camera too", "[freelook]")
{
    const glm::vec3 right = cameraRelativeIntent({4.0f, 0.0f, 0.0f}, kHalfPi); // camera faces +X, so its right is +Z
    CHECK(right.z == Approx(4.0f).margin(1e-4f));
    CHECK(right.x == Approx(0.0f).margin(1e-4f));
    const glm::vec3 back = cameraRelativeIntent({0.0f, 0.0f, 4.0f}, kHalfPi);
    CHECK(back.x == Approx(-4.0f).margin(1e-4f));
}

TEST_CASE("rotating the stick keeps its length and height", "[freelook]")
{
    for (float yaw = -3.0f; yaw <= 3.0f; yaw += 0.7f) {
        const glm::vec3 out = cameraRelativeIntent({1.5f, 0.25f, -2.5f}, yaw);
        CHECK(std::sqrt(out.x * out.x + out.z * out.z) == Approx(std::sqrt(1.5f * 1.5f + 2.5f * 2.5f)).epsilon(1e-4));
        CHECK(out.y == 0.25f);
    }
}

TEST_CASE("hostile movement and yaw become no movement", "[freelook]")
{
    CHECK(cameraRelativeIntent({kNan, 0.0f, 0.0f}, 0.3f) == glm::vec3(0.0f));
    CHECK(cameraRelativeIntent({0.0f, kInf, 0.0f}, 0.3f) == glm::vec3(0.0f));
    CHECK(cameraRelativeIntent({1.0f, 0.0f, 1.0f}, kNan) == glm::vec3(0.0f));
    CHECK(cameraRelativeIntent({1.0f, 0.0f, 1.0f}, kInf) == glm::vec3(0.0f));
}

TEST_CASE("the look axis is sanitised and clamped", "[freelook]")
{
    CHECK(sanitisedLook(kNan) == 0.0f);
    CHECK(sanitisedLook(kInf) == 0.0f);
    CHECK(sanitisedLook(-kInf) == 0.0f);
    CHECK(sanitisedLook(7.0f) == 1.0f);
    CHECK(sanitisedLook(-7.0f) == -1.0f);
    CHECK(sanitisedLook(0.25f) == 0.25f);
}

TEST_CASE("free look turns by the look axis at a bounded rate", "[freelook]")
{
    FacingStep step;
    step.maxTurnDegPerSec = 720.0f;
    step.dt = 1.0f / 60.0f;
    step.look = 1.0f;
    const float oneTick = nextPlayerYaw(step);
    CHECK(oneTick == Approx(kLookRateDegPerSec * kDegToRad / 60.0f).epsilon(1e-4));
    step.look = -1.0f;
    CHECK(nextPlayerYaw(step) == Approx(-oneTick));
    step.look = 100.0f;
    CHECK(nextPlayerYaw(step) == Approx(oneTick)); // an oversized look is no faster than a full one
    step.look = 0.0f;
    step.yawToFoe = 2.0f;
    CHECK(nextPlayerYaw(step) == 0.0f); // free look ignores the foe
}

TEST_CASE("the tracking cap bounds the player's look as it bounds the foe's turn", "[freelook]")
{
    FacingStep step;
    step.look = 1.0f;
    step.dt = 1.0f / 60.0f;
    step.maxTurnDegPerSec = 30.0f; // wind-up
    CHECK(nextPlayerYaw(step) == Approx(30.0f * kDegToRad / 60.0f).epsilon(1e-4));
    step.maxTurnDegPerSec = 0.0f; // recovery
    CHECK(nextPlayerYaw(step) == 0.0f);
}

TEST_CASE("lock-on turns toward the foe, capped by the same clamp", "[freelook]")
{
    FacingStep step;
    step.lockOn = true;
    step.look = 1.0f; // ignored while locked on
    step.yawToFoe = -1.0f;
    step.dt = 1.0f / 60.0f;
    step.maxTurnDegPerSec = 720.0f;
    CHECK(nextPlayerYaw(step) == Approx(-720.0f * kDegToRad / 60.0f).epsilon(1e-4));
    step.maxTurnDegPerSec = 0.0f;
    CHECK(nextPlayerYaw(step) == 0.0f);
}

TEST_CASE("a facing step survives hostile numbers", "[freelook]")
{
    FacingStep step;
    step.maxTurnDegPerSec = 720.0f;
    step.dt = 1.0f / 60.0f;
    step.look = 1.0f;
    step.yaw = kNan;
    CHECK(std::isfinite(nextPlayerYaw(step)));
    step.yaw = 0.0f;
    step.dt = kNan;
    CHECK(nextPlayerYaw(step) == 0.0f);
    step.dt = 1.0f / 60.0f;
    step.lockOn = true;
    step.yawToFoe = kInf;
    CHECK(std::isfinite(nextPlayerYaw(step)));
    CHECK(wrapAngle(kInf) == 0.0f);
    CHECK(wrapAngle(7.0f) == Approx(7.0f - 2.0f * 3.14159265f));
}

TEST_CASE("the view yaw is the facing under free look and the foe direction under lock-on", "[freelook]")
{
    CHECK(viewYaw(false, 0.4f, 2.0f) == 0.4f);
    CHECK(viewYaw(true, 0.4f, 2.0f) == 2.0f);
}

TEST_CASE("the free-look camera sits behind the facing and stays inside the arena", "[freelook][camera]")
{
    const CameraPose north = freeLookCamera({0.0, 0.0, 0.0}, 0.0f);
    CHECK(north.position.z > 1.0); // facing -Z, so behind is +Z
    CHECK(std::abs(north.position.x) < 1e-6);
    const CameraPose east = freeLookCamera({0.0, 0.0, 0.0}, kHalfPi);
    CHECK(east.position.x < -1.0);

    constexpr double limit = kArenaHalf - 0.4;
    for (const float yaw : {0.0f, kHalfPi, 2.0f * kHalfPi, -kHalfPi}) {
        for (const glm::dvec3 at : {glm::dvec3(9.7, 0.0, 9.7), glm::dvec3(-9.7, 0.0, 0.0), glm::dvec3(0.0, 0.0, -9.7)}) {
            const CameraPose pose = freeLookCamera(at, yaw);
            CHECK(std::abs(pose.position.x) <= limit + 1e-9);
            CHECK(std::abs(pose.position.z) <= limit + 1e-9);
        }
    }
    CHECK(std::isfinite(freeLookCamera({0.0, 0.0, 0.0}, kNan).position.x));
}

TEST_CASE("the render rig swings round behind a turning player", "[freelook][camera]")
{
    CameraRig rig;
    rig.updateFree({0.0, 0.0, 0.0}, 0.0f, kDt);
    CameraPose pose;
    for (int i = 0; i < 240; ++i) {
        pose = rig.updateFree({0.0, 0.0, 0.0}, kHalfPi, kDt);
    }
    CHECK(pose.position.x < -1.5);
    CHECK(std::abs(pose.position.z) < 0.3);
}
