#include "ichor/core/rng.h"
#include "ichor/sim/scene.h"
#include "ichor/sim/scene_io.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <limits>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr float kRadius = 0.3f;

SpawnCommand ground()
{
    SpawnCommand spawn;
    spawn.id = StableId::of("ground");
    spawn.name.value = "ground";
    spawn.transform.position = {0.0, -0.5, 0.0};
    spawn.staticBox = StaticBox{glm::vec3(20.0f, 0.5f, 20.0f)};
    return spawn;
}

SpawnCommand wall(double x)
{
    SpawnCommand spawn;
    spawn.id = StableId::of("wall");
    spawn.name.value = "wall";
    spawn.transform.position = {x, 1.0, 0.0};
    spawn.staticBox = StaticBox{glm::vec3(0.5f, 1.0f, 10.0f)};
    return spawn;
}

SpawnCommand hero(glm::dvec3 position)
{
    SpawnCommand spawn;
    spawn.id = StableId::of("hero");
    spawn.name.value = "hero";
    spawn.transform.position = position;
    spawn.player = Player{4.0f};
    spawn.character = CharacterBody{kRadius, 0.6f};
    return spawn;
}

const Transform& heroTransform(const Scene& scene)
{
    return scene.registry().get<Transform>(scene.find(StableId::of("hero")));
}

Transform& heroTransformMutable(Scene& scene)
{
    return scene.registry().get<Transform>(scene.find(StableId::of("hero")));
}

const CharacterBody& heroBody(const Scene& scene)
{
    return scene.registry().get<CharacterBody>(scene.find(StableId::of("hero")));
}

InputStream randomInputs(std::uint64_t seed, std::size_t ticks)
{
    Rng rng(seed);
    InputStream stream;
    for (std::size_t i = 0; i < ticks; ++i) {
        InputFrame frame;
        frame.moveX = static_cast<float>(rng.range(0, 2)) - 1.0f;
        frame.moveY = static_cast<float>(rng.range(0, 2)) - 1.0f;
        frame.buttons = rng.range(0, 1) * InputFrame::Sprint;
        stream.frames.push_back(frame);
    }
    return stream;
}

void run(Scene& scene, const InputStream& inputs)
{
    for (const InputFrame& frame : inputs.frames) {
        scene.tick(kDt, frame);
    }
}

}

TEST_CASE("a character falls onto the ground and rests there")
{
    Scene scene(1);
    scene.enqueue(ground());
    scene.enqueue(hero({0.0, 3.0, 0.0}));
    scene.tick(kDt);
    REQUIRE_FALSE(heroBody(scene).onGround);
    for (int i = 0; i < 240; ++i) {
        scene.tick(kDt);
    }
    REQUIRE(heroBody(scene).onGround);
    REQUIRE(std::abs(heroTransform(scene).position.y) < 0.02);
    REQUIRE(std::abs(heroBody(scene).velocity.y) < 0.01);
}

TEST_CASE("input walks the character and a wall stops it")
{
    Scene scene(1);
    scene.enqueue(ground());
    scene.enqueue(wall(3.0));
    scene.enqueue(hero({0.0, 0.0, 0.0}));
    for (int i = 0; i < 30; ++i) {
        scene.tick(kDt);
    }
    InputFrame right;
    right.moveX = 1.0f;
    for (int i = 0; i < 300; ++i) {
        scene.tick(kDt, right);
    }
    const double x = heroTransform(scene).position.x;
    REQUIRE(x > 1.5);
    REQUIRE(x <= 3.0 - 0.5 - kRadius + 0.05);
    REQUIRE(heroBody(scene).onGround);
    REQUIRE(heroBody(scene).intent.x == 4.0f);
}

TEST_CASE("physics replay is deterministic")
{
    const InputStream inputs = randomInputs(77, 400);
    Scene a(3);
    a.enqueue(ground());
    a.enqueue(wall(-4.0));
    a.enqueue(hero({1.0, 0.5, 1.0}));
    run(a, inputs);

    Scene b(3);
    b.enqueue(ground());
    b.enqueue(wall(-4.0));
    b.enqueue(hero({1.0, 0.5, 1.0}));
    run(b, inputs);

    REQUIRE(a.stateHash() == b.stateHash());
    REQUIRE(heroTransform(a).position == heroTransform(b).position);
    REQUIRE(glm::length(heroTransform(a).position) > 0.5);
}

TEST_CASE("a snapshot restores bodies and the simulation continues identically")
{
    const InputStream inputs = randomInputs(5, 200);
    Scene scene(9);
    scene.enqueue(ground());
    scene.enqueue(wall(5.0));
    scene.enqueue(hero({0.0, 0.0, 0.0}));
    run(scene, inputs);

    const auto path = test::tempPath("ichor_physics.isav");
    REQUIRE(saveScene(path, scene));
    Scene loaded(9);
    REQUIRE(loadScene(path, loaded));
    std::filesystem::remove(path);
    REQUIRE(loaded.stateHash() == scene.stateHash());

    // Jolt's virtual character keeps transient contact state that a snapshot
    // does not carry, so a continued run tracks the original closely rather
    // than bit for bit. Replay from a common start is exact (see above).
    const InputStream more = randomInputs(6, 120);
    run(scene, more);
    run(loaded, more);
    REQUIRE(glm::distance(heroTransform(loaded).position, heroTransform(scene).position) < 0.01);
    REQUIRE(heroBody(loaded).onGround == heroBody(scene).onGround);
    REQUIRE(loaded.registry().all_of<StaticBox>(loaded.find(StableId::of("wall"))));
}

TEST_CASE("restore order does not matter: a character restored before its floor still stands")
{
    // "zzz" sorts after "aaa" by name but ids are hashes; spawn both orders
    // through a snapshot and require the same ground state either way.
    for (const char* floorName : {"aaa", "zzz"}) {
        Scene scene(4);
        SpawnCommand floor = ground();
        floor.id = StableId::of(floorName);
        floor.name.value = floorName;
        scene.enqueue(floor);
        scene.enqueue(hero({0.0, 0.0, 0.0}));
        for (int i = 0; i < 60; ++i) {
            scene.tick(kDt);
        }
        REQUIRE(heroBody(scene).onGround);
        const auto path = test::tempPath("ichor_order.isav");
        REQUIRE(saveScene(path, scene));
        Scene loaded(4);
        REQUIRE(loadScene(path, loaded));
        std::filesystem::remove(path);
        loaded.tick(kDt);
        scene.tick(kDt);
        REQUIRE(heroBody(loaded).onGround);
        REQUIRE(heroBody(loaded).fallSpeed == heroBody(scene).fallSpeed);
        REQUIRE(std::abs(heroTransform(loaded).position.y - heroTransform(scene).position.y) < 1e-6);
    }
}

TEST_CASE("a teleport moves the character body too")
{
    Scene scene(2);
    scene.enqueue(ground());
    scene.enqueue(hero({0.0, 0.0, 0.0}));
    scene.tick(kDt);
    scene.enqueue(MoveCommand{StableId::of("hero"), glm::dvec3(5.0, 0.0, -2.0)});
    for (int i = 0; i < 10; ++i) {
        scene.tick(kDt);
    }
    REQUIRE(std::abs(heroTransform(scene).position.x - 5.0) < 0.05);
    REQUIRE(std::abs(heroTransform(scene).position.z + 2.0) < 0.05);
    REQUIRE(heroBody(scene).onGround);
}

TEST_CASE("previous transforms lag one tick for interpolation")
{
    Scene scene(2);
    scene.enqueue(hero({0.0, 10.0, 0.0}));
    scene.tick(kDt);
    scene.tick(kDt);
    const entt::entity e = scene.find(StableId::of("hero"));
    const auto& previous = scene.registry().get<PreviousTransform>(e);
    const auto& current = scene.registry().get<Transform>(e);
    REQUIRE(previous.position.y > current.position.y);
    REQUIRE(previous.position.y <= 10.0);
}

TEST_CASE("debug lines describe every body")
{
    Scene scene(2);
    scene.enqueue(ground());
    scene.enqueue(hero({0.0, 0.0, 0.0}));
    scene.tick(kDt);
    std::vector<LineSegment> lines;
    scene.physics().debugLines(lines);
    REQUIRE(lines.size() > 12);
}

namespace {

constexpr double kBadValues[] = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

bool finite(glm::dvec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

std::uint64_t tickedHashAfterBadPosition(double bad, int axis)
{
    Scene scene(4);
    scene.enqueue(ground());
    scene.enqueue(hero({1.0, 0.5, 1.0}));
    scene.tick(kDt);
    heroTransformMutable(scene).position[axis] = bad;
    for (int i = 0; i < 5; ++i) {
        scene.tick(kDt);
    }
    REQUIRE(finite(heroTransform(scene).position));
    REQUIRE(std::isfinite(heroBody(scene).velocity.y));
    REQUIRE(std::isfinite(heroBody(scene).fallSpeed));
    return scene.stateHash();
}

}

TEST_CASE("a non-finite player position never reaches the physics step and heals to the last good body position")
{
    for (const double bad : kBadValues) {
        for (int axis = 0; axis < 3; ++axis) {
            const std::uint64_t first = tickedHashAfterBadPosition(bad, axis);
            REQUIRE(first == tickedHashAfterBadPosition(bad, axis));
        }
    }
}

TEST_CASE("a non-finite teleport leaves the character where it was")
{
    for (const double bad : kBadValues) {
        Scene scene(2);
        scene.enqueue(ground());
        scene.enqueue(hero({1.0, 0.0, 1.0}));
        for (int i = 0; i < 10; ++i) {
            scene.tick(kDt);
        }
        SetTransformCommand command;
        command.id = StableId::of("hero");
        command.transform.position = {bad, 0.0, bad};
        scene.enqueue(command);
        for (int i = 0; i < 10; ++i) {
            scene.tick(kDt);
        }
        REQUIRE(finite(heroTransform(scene).position));
        REQUIRE(std::abs(heroTransform(scene).position.x - 1.0) < 0.05);
        REQUIRE(heroBody(scene).onGround);
    }
}

TEST_CASE("spawning with a non-finite position drops the body and the scene still ticks")
{
    for (const double bad : kBadValues) {
        Scene scene(2);
        scene.enqueue(ground());
        SpawnCommand badWall = wall(bad);
        badWall.id = StableId::of("bad-wall");
        scene.enqueue(badWall);
        SpawnCommand badHero = hero({bad, 0.0, 0.0});
        badHero.id = StableId::of("bad-hero");
        scene.enqueue(badHero);
        scene.enqueue(hero({0.0, 0.0, 0.0}));
        for (int i = 0; i < 20; ++i) {
            scene.tick(kDt);
        }
        REQUIRE_FALSE(scene.physics().hasBody(StableId::of("bad-wall")));
        REQUIRE_FALSE(scene.physics().hasBody(StableId::of("bad-hero")));
        REQUIRE(scene.physics().hasBody(StableId::of("hero")));
        REQUIRE(finite(heroTransform(scene).position));
    }
}
