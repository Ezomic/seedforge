#include "ichor/editor/nudge.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <limits>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kHalfPi = 1.5707963f;

PickCamera facing(float yaw, float pitch = 0.0f)
{
    PickCamera camera;
    camera.rotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(pitch, glm::vec3(1, 0, 0));
    return camera;
}

glm::dvec3 offsetOf(NudgeKeys keys, const PickCamera& camera)
{
    const auto offset = nudgeOffset(keys, camera);
    REQUIRE(offset.has_value());
    return *offset;
}

}

TEST_CASE("forward goes the way the camera faces and left goes screen left")
{
    NudgeKeys forward;
    forward.forward = true;
    NudgeKeys left;
    left.left = true;

    const glm::dvec3 ahead = offsetOf(forward, facing(0.0f));
    REQUIRE(ahead.x == Approx(0.0).margin(1e-9));
    REQUIRE(ahead.z == Approx(-0.5));

    // Turned a quarter to the left the camera faces -X, so screen left is +Z.
    const glm::dvec3 turned = offsetOf(left, facing(kHalfPi));
    REQUIRE(turned.x == Approx(0.0).margin(1e-6));
    REQUIRE(turned.z == Approx(0.5));
    REQUIRE(offsetOf(forward, facing(kHalfPi)).x == Approx(-0.5));
}

TEST_CASE("the right key is the opposite of left and back of forward")
{
    NudgeKeys right;
    right.right = true;
    REQUIRE(offsetOf(right, facing(0.0f)).x == Approx(0.5));
    NudgeKeys back;
    back.back = true;
    REQUIRE(offsetOf(back, facing(0.0f)).z == Approx(0.5));
}

TEST_CASE("up and down move along Y and pitch does not tilt a ground nudge")
{
    NudgeKeys up;
    up.up = true;
    REQUIRE(offsetOf(up, facing(0.4f, 0.3f)) == glm::dvec3(0.0, 0.5, 0.0));
    NudgeKeys down;
    down.down = true;
    REQUIRE(offsetOf(down, facing(0.4f)) == glm::dvec3(0.0, -0.5, 0.0));

    NudgeKeys forward;
    forward.forward = true;
    const glm::dvec3 ahead = offsetOf(forward, facing(0.4f, -0.6f));
    REQUIRE(ahead.y == 0.0);
    REQUIRE(glm::length(ahead) == Approx(0.5));
}

TEST_CASE("shift makes the step ten times longer")
{
    NudgeKeys keys;
    keys.forward = true;
    keys.up = true;
    keys.fast = true;
    const glm::dvec3 offset = offsetOf(keys, facing(0.0f));
    REQUIRE(offset.z == Approx(-5.0));
    REQUIRE(offset.y == Approx(5.0));
}

TEST_CASE("a diagonal is not faster than a straight step")
{
    NudgeKeys keys;
    keys.forward = true;
    keys.right = true;
    const glm::dvec3 offset = offsetOf(keys, facing(0.7f));
    REQUIRE(glm::length(offset) == Approx(0.5));
    REQUIRE(offset.y == 0.0);
}

TEST_CASE("opposite keys cancel and no key asks for nothing")
{
    NudgeKeys keys;
    REQUIRE_FALSE(nudgeOffset(keys, facing(0.0f)).has_value());
    keys.left = true;
    keys.right = true;
    keys.up = true;
    keys.down = true;
    REQUIRE_FALSE(nudgeOffset(keys, facing(0.0f)).has_value());
}

TEST_CASE("looking straight down or up still nudges along the screen")
{
    NudgeKeys forward;
    forward.forward = true;
    NudgeKeys right;
    right.right = true;

    // Pitched down, the top of the screen is where the camera was heading before it tipped: -Z.
    const PickCamera down = facing(0.0f, -kHalfPi);
    REQUIRE(offsetOf(forward, down).z == Approx(-0.5).margin(1e-6));
    REQUIRE(offsetOf(right, down).x == Approx(0.5).margin(1e-6));

    const PickCamera up = facing(0.0f, kHalfPi);
    REQUIRE(offsetOf(forward, up).z == Approx(-0.5).margin(1e-6));
    REQUIRE(offsetOf(right, up).x == Approx(0.5).margin(1e-6));

    const PickCamera turnedDown = facing(kHalfPi, -kHalfPi);
    REQUIRE(offsetOf(forward, turnedDown).x == Approx(-0.5).margin(1e-6));
}

TEST_CASE("a camera that is not numbers asks for nothing")
{
    NudgeKeys keys;
    keys.forward = true;
    PickCamera camera;
    camera.rotation = glm::quat(kNaN, 0.0f, 0.0f, 0.0f);
    REQUIRE_FALSE(nudgeOffset(keys, camera).has_value());
    camera.rotation = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE_FALSE(nudgeOffset(keys, camera).has_value());
    camera = PickCamera{};
    camera.position.x = std::numeric_limits<double>::infinity();
    REQUIRE_FALSE(nudgeOffset(keys, camera).has_value());
}
