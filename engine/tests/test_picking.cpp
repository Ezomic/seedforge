#include "ichor/editor/picking.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

StableId put(Scene& scene, const char* name, glm::dvec3 position, glm::vec3 halfExtents = {1.0f, 1.0f, 1.0f})
{
    SpawnCommand spawn;
    spawn.id = StableId::of(name);
    spawn.name.value = name;
    spawn.transform.position = position;
    spawn.staticBox = StaticBox{halfExtents};
    scene.enqueue(spawn);
    scene.tick(1.0 / 60.0);
    return spawn.id;
}

PickRay forward(glm::dvec3 origin)
{
    return {origin, {0.0, 0.0, -1.0}};
}

}

TEST_CASE("the centre pixel looks straight ahead")
{
    PickCamera camera;
    camera.position = {1.0, 2.0, 3.0};
    const auto ray = pickRay({400.0, 300.0}, {800.0, 600.0}, camera);
    REQUIRE(ray.has_value());
    REQUIRE(ray->origin == camera.position);
    REQUIRE(ray->direction.x == Approx(0.0).margin(1e-9));
    REQUIRE(ray->direction.y == Approx(0.0).margin(1e-9));
    REQUIRE(ray->direction.z == Approx(-1.0));
}

TEST_CASE("pixels to the right of and above the centre point right and up")
{
    const auto ray = pickRay({600.0, 100.0}, {800.0, 600.0}, PickCamera{});
    REQUIRE(ray.has_value());
    REQUIRE(ray->direction.x > 0.0);
    REQUIRE(ray->direction.y > 0.0);
    REQUIRE(glm::length(ray->direction) == Approx(1.0));
}

TEST_CASE("the top edge sits half the field of view above the axis")
{
    PickCamera camera;
    camera.fovY = 1.0f;
    const auto ray = pickRay({400.0, 0.0}, {800.0, 600.0}, camera);
    REQUIRE(ray.has_value());
    REQUIRE(std::atan2(ray->direction.y, -ray->direction.z) == Approx(0.5));
}

TEST_CASE("the ray follows the rotation of the camera")
{
    PickCamera camera;
    camera.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)); // turned left, to look down -X
    const auto ray = pickRay({400.0, 300.0}, {800.0, 600.0}, camera);
    REQUIRE(ray.has_value());
    REQUIRE(ray->direction.x == Approx(-1.0));
}

TEST_CASE("a cursor outside the window gives no ray")
{
    const glm::dvec2 viewport{800.0, 600.0};
    REQUIRE_FALSE(pickRay({-1.0, 300.0}, viewport, PickCamera{}).has_value());
    REQUIRE_FALSE(pickRay({400.0, -1.0}, viewport, PickCamera{}).has_value());
    REQUIRE_FALSE(pickRay({801.0, 300.0}, viewport, PickCamera{}).has_value());
    REQUIRE_FALSE(pickRay({400.0, 601.0}, viewport, PickCamera{}).has_value());
}

TEST_CASE("a viewport without area or with odd values gives no ray")
{
    REQUIRE_FALSE(pickRay({0.0, 0.0}, {0.0, 0.0}, PickCamera{}).has_value());
    REQUIRE_FALSE(pickRay({0.0, 0.0}, {800.0, 0.0}, PickCamera{}).has_value());
    REQUIRE_FALSE(pickRay({0.0, 0.0}, {-800.0, 600.0}, PickCamera{}).has_value());
    REQUIRE_FALSE(pickRay({0.0, 0.0}, {kNaN, 600.0}, PickCamera{}).has_value());
    REQUIRE_FALSE(pickRay({kNaN, 10.0}, {800.0, 600.0}, PickCamera{}).has_value());
    REQUIRE_FALSE(pickRay({10.0, std::numeric_limits<double>::infinity()}, {800.0, 600.0}, PickCamera{}).has_value());
}

TEST_CASE("a camera that is not a camera gives no ray")
{
    PickCamera camera;
    camera.fovY = 0.0f;
    REQUIRE_FALSE(pickRay({10.0, 10.0}, {800.0, 600.0}, camera).has_value());
    camera.fovY = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(pickRay({10.0, 10.0}, {800.0, 600.0}, camera).has_value());
    camera = PickCamera{};
    camera.position.y = kNaN;
    REQUIRE_FALSE(pickRay({10.0, 10.0}, {800.0, 600.0}, camera).has_value());
    camera = PickCamera{};
    camera.rotation = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE_FALSE(pickRay({10.0, 10.0}, {800.0, 600.0}, camera).has_value());
}

TEST_CASE("the nearest entity on the ray wins")
{
    Scene scene(1);
    put(scene, "far", {0.0, 0.0, -20.0});
    const StableId near = put(scene, "near", {0.0, 0.0, -10.0});
    REQUIRE(pickEntity(scene, forward({0.0, 0.0, 0.0}), {}) == near);
}

TEST_CASE("a ray that misses everything picks nothing, and so does an empty scene")
{
    Scene scene(1);
    REQUIRE_FALSE(pickEntity(scene, forward({0.0, 0.0, 0.0}), {}).has_value());
    put(scene, "rock", {5.0, 0.0, -10.0});
    REQUIRE_FALSE(pickEntity(scene, forward({0.0, 0.0, 0.0}), {}).has_value());
}

TEST_CASE("something behind the camera is not picked")
{
    Scene scene(1);
    put(scene, "behind", {0.0, 0.0, 10.0});
    REQUIRE_FALSE(pickEntity(scene, forward({0.0, 0.0, 0.0}), {}).has_value());
}

TEST_CASE("a ray that starts inside a box picks it")
{
    Scene scene(1);
    const StableId room = put(scene, "room", {0.0, 0.0, 0.0}, {5.0f, 5.0f, 5.0f});
    put(scene, "pillar", {0.0, 0.0, -3.0});
    REQUIRE(pickEntity(scene, forward({0.0, 0.0, 0.0}), {}) == room);
}

TEST_CASE("equal distances go to the lowest id whichever was made first")
{
    const StableId low = std::min(StableId::of("twin a"), StableId::of("twin b"));
    for (const bool reversed : {false, true}) {
        Scene scene(1);
        put(scene, reversed ? "twin b" : "twin a", {0.0, 0.0, -10.0});
        put(scene, reversed ? "twin a" : "twin b", {0.0, 0.0, -10.0});
        REQUIRE(pickEntity(scene, forward({0.0, 0.0, 0.0}), {}) == low);
    }
}

TEST_CASE("a ray parallel to a face either grazes inside the slab or misses")
{
    Scene scene(1);
    const StableId slab = put(scene, "slab", {0.0, 0.0, -10.0}, {5.0f, 1.0f, 1.0f});
    REQUIRE(pickEntity(scene, {{0.0, 0.5, 0.0}, {0.0, 0.0, -1.0}}, {}) == slab);
    REQUIRE_FALSE(pickEntity(scene, {{0.0, 1.5, 0.0}, {0.0, 0.0, -1.0}}, {}).has_value());
    REQUIRE_FALSE(pickEntity(scene, {{0.0, 1.0 + 1e-6, 0.0}, {0.0, 0.0, -1.0}}, {}).has_value());
}

TEST_CASE("the box turns with the entity and grows with its scale")
{
    Scene scene(1);
    SpawnCommand spawn;
    spawn.id = StableId::of("plank");
    spawn.name.value = "plank";
    spawn.transform.position = {0.0, 0.0, -10.0};
    spawn.transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    spawn.transform.scale = {1.0f, 1.0f, 2.0f};
    spawn.staticBox = StaticBox{{0.5f, 0.5f, 3.0f}}; // local z reaches 6 each way once scaled, and the turn lays it along world x
    scene.enqueue(spawn);
    scene.tick(1.0 / 60.0);
    // Looking down -Z from x = 5 only crosses the plank if its long axis lies along x.
    REQUIRE(pickEntity(scene, forward({5.0, 0.0, 0.0}), {}) == spawn.id);
    REQUIRE_FALSE(pickEntity(scene, forward({7.0, 0.0, 0.0}), {}).has_value());
}

TEST_CASE("a mesh uses its bounds, and without them or a box there is nothing to hit")
{
    Scene scene(1);
    SpawnCommand spawn;
    spawn.id = StableId::of("statue");
    spawn.name.value = "statue";
    spawn.transform.position = {0.0, 0.0, -10.0};
    spawn.mesh = MeshRef{1};
    scene.enqueue(spawn);
    scene.tick(1.0 / 60.0);
    const std::vector<PickBounds> meshes = {{}, {{-1.0f, 0.0f, -1.0f}, {1.0f, 4.0f, 1.0f}}};
    REQUIRE(pickEntity(scene, forward({0.0, 3.0, 0.0}), meshes) == spawn.id);
    REQUIRE_FALSE(pickEntity(scene, forward({0.0, -1.0, 0.0}), meshes).has_value());
    REQUIRE_FALSE(pickEntity(scene, forward({0.0, 3.0, 0.0}), {}).has_value());
}

TEST_CASE("a degenerate scale or ray is skipped, not trusted")
{
    Scene scene(1);
    SpawnCommand spawn;
    spawn.id = StableId::of("flat");
    spawn.name.value = "flat";
    spawn.transform.position = {0.0, 0.0, -10.0};
    spawn.transform.scale = {1.0f, 0.0f, 1.0f};
    spawn.staticBox = StaticBox{{1.0f, 1.0f, 1.0f}};
    scene.enqueue(spawn);
    scene.tick(1.0 / 60.0);
    REQUIRE_FALSE(pickEntity(scene, forward({0.0, 0.0, 0.0}), {}).has_value());
    put(scene, "solid", {0.0, 0.0, -20.0});
    REQUIRE(pickEntity(scene, forward({0.0, 0.0, 0.0}), {}) == StableId::of("solid"));
    REQUIRE_FALSE(pickEntity(scene, {{0.0, 0.0, 0.0}, {kNaN, 0.0, -1.0}}, {}).has_value());
    REQUIRE_FALSE(pickEntity(scene, {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, {}).has_value());
}

TEST_CASE("a mesh with inverted bounds, as an empty mesh has, cannot be hit")
{
    Scene scene(1);
    SpawnCommand spawn;
    spawn.id = StableId::of("empty");
    spawn.name.value = "empty";
    spawn.transform.position = {0.0, 0.0, -10.0};
    spawn.mesh = MeshRef{0};
    scene.enqueue(spawn);
    scene.tick(1.0 / 60.0);
    const float big = std::numeric_limits<float>::max();
    const std::vector<PickBounds> meshes = {{{big, big, big}, {-big, -big, -big}}};
    REQUIRE_FALSE(pickEntity(scene, forward({0.0, 0.0, 0.0}), meshes).has_value());
}
