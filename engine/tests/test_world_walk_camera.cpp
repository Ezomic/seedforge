#include "ichor/gfx/walk_camera.h"
#include "ichor/sim/scene.h"

#include <catch2/catch_test_macros.hpp>

#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr double kDt = 1.0 / 60.0;

glm::dvec3 walked(glm::dvec3 start, InputFrame input, int ticks, float yaw = 0.0f)
{
    Scene scene(1);
    SpawnCommand spawn;
    spawn.id = StableId::of("player");
    spawn.name.value = "player";
    spawn.transform.position = start;
    spawn.player = Player{4.0f};
    spawn.character = CharacterBody{};
    scene.enqueue(spawn);
    // The app's walk_look system, which runs between the input and the physics step.
    scene.addSystem("walk_look", [yaw](Scene& s, double, const InputFrame&) {
        for (auto [entity, player, body] : s.registry().view<const Player, CharacterBody>().each()) body.intent = walkRelativeIntent(body.intent, yaw);
    });
    scene.tick(kDt);
    for (int i = 0; i < ticks; ++i) scene.tick(kDt, input);
    return scene.registry().get<Transform>(scene.find(StableId::of("player"))).position - start;
}

glm::dvec3 walkedRelative(glm::dvec3 start, InputFrame input, float yaw)
{
    return walked(start, input, 30, yaw);
}

}

TEST_CASE("W walks where the walking camera looks, wherever the player stands", "[camera][direction]")
{
    const glm::vec3 look = walkCameraRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::dvec2 ahead = glm::normalize(glm::dvec2(look.x, look.z));
    InputFrame w;
    w.moveY = 1.0f;
    InputFrame s;
    s.moveY = -1.0f;
    InputFrame d;
    d.moveX = 1.0f;
    const glm::vec3 rightAxis = walkCameraRotation() * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::dvec2 right = glm::normalize(glm::dvec2(rightAxis.x, rightAxis.z));

    // Includes positions on both sides of the old fixed aim point, where W used to turn into S.
    for (const glm::dvec3 start : {glm::dvec3(2.2, 0.0, 2.8), glm::dvec3(0.0, 0.0, 0.0), glm::dvec3(-3.0, 0.0, -6.0), glm::dvec3(5.0, 0.0, -20.0)}) {
        const glm::dvec3 forward = walked(start, w, 30);
        const glm::dvec3 back = walked(start, s, 30);
        const glm::dvec3 strafe = walked(start, d, 30);
        CHECK(glm::dot(glm::normalize(glm::dvec2(forward.x, forward.z)), ahead) > 0.99);
        CHECK(glm::dot(glm::normalize(glm::dvec2(back.x, back.z)), ahead) < -0.99);
        CHECK(glm::dot(glm::normalize(glm::dvec2(strafe.x, strafe.z)), right) > 0.99);
    }
}

TEST_CASE("the walking camera looks level with the world axes and slightly down", "[camera][direction]")
{
    const glm::vec3 look = walkCameraRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(std::abs(look.x) < 1e-5f);
    CHECK(look.z < -0.9f);
    CHECK(look.y < 0.0f);
}

TEST_CASE("W walks where the turned camera looks, at several yaws and positions", "[camera][direction]")
{
    for (const float look : {0.0f, 0.4f, 1.0f, -0.7f, -1.0f}) {
        for (const int turnTicks : {0, 20, 70, 200}) {
            float yaw = 0.0f;
            for (int i = 0; i < turnTicks; ++i) yaw = nextWalkYaw(yaw, look, kDt);
            const glm::vec3 view = walkCameraRotation(yaw) * glm::vec3(0.0f, 0.0f, -1.0f);
            const glm::dvec2 ahead = glm::normalize(glm::dvec2(view.x, view.z));
            const glm::vec3 side = walkCameraRotation(yaw) * glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::dvec2 right = glm::normalize(glm::dvec2(side.x, side.z));
            for (const glm::dvec3 start : {glm::dvec3(0.0, 0.0, 0.0), glm::dvec3(-3.0, 0.0, -6.0)}) {
                InputFrame w;
                w.moveY = 1.0f;
                InputFrame d;
                d.moveX = 1.0f;
                const glm::dvec3 forward = walkedRelative(start, w, yaw);
                const glm::dvec3 strafe = walkedRelative(start, d, yaw);
                INFO("yaw " << yaw);
                CHECK(glm::dot(glm::normalize(glm::dvec2(forward.x, forward.z)), ahead) > 0.99);
                CHECK(glm::dot(glm::normalize(glm::dvec2(strafe.x, strafe.z)), right) > 0.99);
            }
        }
    }
}

TEST_CASE("the walk yaw follows the look axis and ignores hostile input", "[camera][direction]")
{
    CHECK(nextWalkYaw(0.0f, 1.0f, 0.5) == Approx(120.0f * 3.14159265f / 180.0f).epsilon(1e-4));
    CHECK(nextWalkYaw(0.0f, -1.0f, 0.5) < 0.0f);
    CHECK(nextWalkYaw(0.0f, 5.0f, 0.5) == nextWalkYaw(0.0f, 1.0f, 0.5));
    CHECK(nextWalkYaw(0.5f, std::numeric_limits<float>::quiet_NaN(), kDt) == 0.5f);
    CHECK(nextWalkYaw(std::numeric_limits<float>::infinity(), 1.0f, kDt) == 0.0f);
    CHECK(std::abs(nextWalkYaw(3.1f, 1.0f, 0.5)) <= 3.1416f);
}

TEST_CASE("without a turn the walking intent is untouched, bit for bit", "[camera][direction]")
{
    const glm::vec3 intent(1.25f, 0.5f, -4.0f);
    CHECK(walkRelativeIntent(intent, 0.0f) == intent);
}
