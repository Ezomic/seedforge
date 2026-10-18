#include "ichor/stage0/camera_rig.h"
#include "ichor/stage0/combat.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <cmath>
#include <limits>

using namespace ichor::stage0;

namespace {

constexpr double kLimit = kArenaHalf - 0.4;

bool insideArena(glm::dvec3 at)
{
    return std::abs(at.x) <= kLimit + 1e-9 && std::abs(at.z) <= kLimit + 1e-9;
}

CameraPose follow(glm::dvec3 player, glm::dvec3 foe, CameraFraming framing)
{
    CameraTuning tuning;
    tuning.framing = framing;
    return frameCamera(player, foe, awayFrom(player, foe), tuning);
}

}

TEST_CASE("the camera never leaves the arena, whichever wall the player stands against", "[camera]")
{
    for (const CameraFraming framing : {CameraFraming::Follow, CameraFraming::LockOn}) {
        for (const double edge : {-9.7, 9.7}) {
            CHECK(insideArena(follow({edge, 0.0, 0.0}, {0.0, 0.0, 0.0}, framing).position));
            CHECK(insideArena(follow({0.0, 0.0, edge}, {0.0, 0.0, 0.0}, framing).position));
            CHECK(insideArena(follow({edge, 0.0, edge}, {-edge, 0.0, -edge}, framing).position));
        }
    }
}

TEST_CASE("with the player against the wall the unclamped boom would be outside the arena", "[camera]")
{
    const CameraPose pose = follow({0.0, 0.0, 9.5}, {0.0, 0.0, 0.0}, CameraFraming::Follow);
    CHECK(pose.position.z <= kLimit + 1e-9);
    CHECK(pose.position.y > 2.0);
}

TEST_CASE("the default boom is the old fixed 3.2 m behind and 2 m up in open space", "[camera]")
{
    const CameraPose pose = followCamera({0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    CHECK(pose.position.z == Catch::Approx(6.2));
    CHECK(pose.position.y == 2.0);
}

TEST_CASE("distance and height are tuning", "[camera]")
{
    CameraTuning tuning;
    tuning.distance = 5.0f;
    tuning.height = 3.0f;
    const CameraPose pose = frameCamera({0.0, 0.0, 0.0}, {0.0, 0.0, -2.0}, {0.0, 0.0, 1.0}, tuning);
    CHECK(pose.position.z == Catch::Approx(5.0));
    CHECK(pose.position.y == 3.0);
}

TEST_CASE("lock-on keeps both fighters in view anywhere in the arena", "[camera]")
{
    int checked = 0;
    for (double px = -9.5; px <= 9.5; px += 3.8) {
        for (double pz = -9.5; pz <= 9.5; pz += 3.8) {
            for (double fx = -9.5; fx <= 9.5; fx += 3.8) {
                for (double fz = -9.5; fz <= 9.5; fz += 3.8) {
                    const glm::dvec3 player{px, 0.0, pz};
                    const glm::dvec3 foe{fx, 0.0, fz};
                    if (glm::length(player - foe) < 1.0) {
                        continue;
                    }
                    const CameraPose pose = follow(player, foe, CameraFraming::LockOn);
                    CHECK(inFrustum(pose, player + glm::dvec3(0.0, 0.9, 0.0), 0.3f));
                    CHECK(inFrustum(pose, foe + glm::dvec3(0.0, 0.9, 0.0), 0.3f));
                    ++checked;
                }
            }
        }
    }
    CHECK(checked > 100);
}

TEST_CASE("lock-on pulls back further the further apart the fighters are", "[camera]")
{
    const double near = follow({0.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, CameraFraming::LockOn).position.z - 1.0;
    const double far = follow({0.0, 0.0, 4.0}, {0.0, 0.0, 0.0}, CameraFraming::LockOn).position.z - 4.0;
    CHECK(far > near);
}

TEST_CASE("smoothing swings the boom gradually and ends at the target", "[camera]")
{
    CameraRig rig;
    const glm::dvec3 foe{0.0, 0.0, -2.0};
    const CameraPose first = rig.update({0.0, 0.0, 3.0}, foe, 1.0 / 60.0);
    CHECK(first.position.z > 3.0);
    // The player runs round to the other side of the foe.
    const CameraPose step = rig.update({0.0, 0.0, -7.0}, foe, 1.0 / 60.0);
    const CameraPose ideal = followCamera({0.0, 0.0, -7.0}, foe);
    CHECK(step.position != ideal.position);
    CameraPose last = step;
    for (int i = 0; i < 600; ++i) {
        last = rig.update({0.0, 0.0, -7.0}, foe, 1.0 / 60.0);
    }
    CHECK(std::abs(last.position.z - ideal.position.z) < 1e-3);
}

TEST_CASE("the rig is deterministic", "[camera]")
{
    CameraRig a;
    CameraRig b;
    CameraPose pa;
    CameraPose pb;
    for (int i = 0; i < 200; ++i) {
        const glm::dvec3 player{std::sin(i * 0.1) * 5.0, 0.0, std::cos(i * 0.1) * 5.0};
        pa = a.update(player, {0.0, 0.0, 0.0}, 1.0 / 60.0);
        pb = b.update(player, {0.0, 0.0, 0.0}, 1.0 / 60.0);
    }
    CHECK(pa.position == pb.position);
    CHECK(pa.rotation == pb.rotation);
}

TEST_CASE("non-finite or degenerate input never produces a non-finite pose", "[camera]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CameraRig rig;
    const CameraPose good = rig.update({0.0, 0.0, 3.0}, {0.0, 0.0, -2.0}, 1.0 / 60.0);
    CHECK(rig.update({nan, 0.0, 0.0}, {0.0, 0.0, 0.0}, 1.0 / 60.0).position == good.position);
    CHECK(rig.update({0.0, 0.0, 0.0}, {inf, 0.0, 0.0}, 1.0 / 60.0).position == good.position);
    CHECK(rig.update({0.0, 0.0, 3.0}, {0.0, 0.0, -2.0}, nan).position == good.position);
    const CameraPose bad = followCamera({nan, 0.0, 0.0}, {0.0, 0.0, 0.0});
    CHECK(std::isfinite(bad.position.x));
    const CameraPose same = followCamera({1.0, 0.0, 1.0}, {1.0, 0.0, 1.0});
    CHECK(std::isfinite(same.position.z));
    const CameraPose far = followCamera({1e9, 0.0, 1e9}, {-1e9, 0.0, 0.0});
    CHECK(insideArena(far.position));
}

TEST_CASE("smoothAway moves toward the target, never past it, and settles on it", "[camera]")
{
    const glm::dvec3 target{1.0, 0.0, 0.0};
    glm::dvec3 current{0.0, 0.0, 1.0};
    double previous = glm::length(current - target);
    for (int i = 0; i < 300; ++i) {
        current = smoothAway(current, target, 1.0 / 60.0, 8.0f);
        const double now = glm::length(current - target);
        CHECK(now < previous);
        previous = now;
    }
    CHECK(previous < 1e-3);
    const glm::dvec3 huge = smoothAway({0.0, 0.0, 1.0}, target, 1e12, 8.0f);
    CHECK(glm::length(huge - target) < 1e-6);
    const glm::dvec3 negative = smoothAway({0.0, 0.0, 1.0}, target, -5.0, 8.0f);
    CHECK(negative == glm::dvec3(0.0, 0.0, 1.0));
}
