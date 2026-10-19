#include "ichor/gfx/walk_camera.h"
#include "ichor/sim/scene.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;

glm::dvec3 walked(glm::dvec3 start, InputFrame input, int ticks)
{
    Scene scene(1);
    SpawnCommand spawn;
    spawn.id = StableId::of("player");
    spawn.name.value = "player";
    spawn.transform.position = start;
    spawn.player = Player{4.0f};
    scene.enqueue(spawn);
    scene.tick(kDt);
    for (int i = 0; i < ticks; ++i) scene.tick(kDt, input);
    return scene.registry().get<Transform>(scene.find(StableId::of("player"))).position - start;
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
