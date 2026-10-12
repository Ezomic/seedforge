#include "ichor/core/rng.h"
#include "ichor/sim/scene.h"
#include "ichor/sim/scene_io.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;

SpawnCommand piece(std::string_view name, std::uint64_t index, glm::dvec3 position)
{
    SpawnCommand spawn;
    spawn.id = StableId::of(name, index);
    spawn.name.value = std::string(name);
    spawn.transform.position = position;
    spawn.mesh = MeshRef{static_cast<std::uint32_t>(index)};
    spawn.spin = Spin{glm::vec3(0.0f, 1.0f, 0.0f), 1.0f + static_cast<float>(index)};
    return spawn;
}

SpawnCommand player()
{
    SpawnCommand spawn;
    spawn.id = StableId::of("player");
    spawn.name.value = "player";
    spawn.player = Player{4.0f};
    return spawn;
}

InputStream randomInputs(std::uint64_t seed, std::size_t ticks)
{
    Rng rng(seed);
    InputStream stream;
    for (std::size_t i = 0; i < ticks; ++i) {
        InputFrame frame;
        frame.moveX = static_cast<float>(rng.range(0, 2)) - 1.0f;
        frame.moveY = static_cast<float>(rng.range(0, 2)) - 1.0f;
        frame.buttons = rng.range(0, 7);
        stream.frames.push_back(frame);
    }
    return stream;
}

void populate(Scene& scene)
{
    scene.enqueue(piece("aspis", 0, {-1.0, 0.0, 0.0}));
    scene.enqueue(piece("dory", 1, {0.0, 0.0, 0.0}));
    scene.enqueue(piece("helmet", 2, {1.0, 0.0, 0.0}));
    scene.enqueue(player());
}

std::uint64_t runWithInputs(Scene& scene, const InputStream& inputs)
{
    for (const InputFrame& frame : inputs.frames) {
        scene.tick(kDt, frame);
    }
    return scene.stateHash();
}

}

TEST_CASE("commands spawn, move and destroy entities by stable id")
{
    Scene scene(5);
    populate(scene);
    REQUIRE(scene.registry().view<const StableId>().size() == 0);
    scene.tick(kDt);
    REQUIRE(scene.registry().view<const StableId>().size() == 4);
    REQUIRE((scene.find(StableId::of("dory", 1)) != entt::null));
    REQUIRE((scene.find(StableId::of("nothing")) == entt::null));

    scene.enqueue(MoveCommand{StableId::of("dory", 1), glm::dvec3(0.0, 2.0, 0.0)});
    scene.tick(kDt);
    REQUIRE(scene.registry().get<Transform>(scene.find(StableId::of("dory", 1))).position.y == 2.0);

    scene.enqueue(DestroyCommand{StableId::of("aspis", 0)});
    scene.tick(kDt);
    REQUIRE((scene.find(StableId::of("aspis", 0)) == entt::null));
    REQUIRE(scene.log().entries.size() == 6);
}

TEST_CASE("the state hash ignores spawn order")
{
    Scene a(9);
    a.enqueue(piece("x", 0, {0.0, 0.0, 0.0}));
    a.enqueue(piece("y", 1, {1.0, 0.0, 0.0}));
    a.tick(kDt);

    Scene b(9);
    b.enqueue(piece("y", 1, {1.0, 0.0, 0.0}));
    b.enqueue(piece("x", 0, {0.0, 0.0, 0.0}));
    b.tick(kDt);

    REQUIRE(a.stateHash() == b.stateHash());

    Scene c(9);
    c.enqueue(piece("x", 0, {0.0, 0.0, 0.0}));
    c.enqueue(piece("y", 1, {1.0, 0.0, 1.0}));
    c.tick(kDt);
    REQUIRE(a.stateHash() != c.stateHash());
}

TEST_CASE("player input moves the player and is recorded as commands")
{
    Scene scene(1);
    scene.enqueue(player());
    scene.tick(kDt);
    InputFrame forward;
    forward.moveY = 1.0f;
    for (int i = 0; i < 60; ++i) {
        scene.tick(kDt, forward);
    }
    const auto& position = scene.registry().get<Transform>(scene.find(StableId::of("player"))).position;
    REQUIRE(position.z < -3.9);
    REQUIRE(position.z > -4.1);
    REQUIRE(scene.log().entries.size() == 61);
}

TEST_CASE("replaying a recorded input stream reproduces the state hash")
{
    const InputStream inputs = randomInputs(42, 300);

    Scene first(7);
    populate(first);
    const std::uint64_t expected = runWithInputs(first, inputs);

    Scene second(7);
    populate(second);
    REQUIRE(runWithInputs(second, inputs) == expected);

    Scene other(7);
    populate(other);
    REQUIRE(runWithInputs(other, randomInputs(43, 300)) != expected);
}

TEST_CASE("replaying the command log reproduces the state hash")
{
    const InputStream inputs = randomInputs(11, 120);
    Scene first(3);
    populate(first);
    first.enqueue(DestroyCommand{StableId::of("helmet", 2)});
    const std::uint64_t expected = runWithInputs(first, inputs);

    // The log holds spawns, moves from input, and the destroy, each stamped
    // with its tick. Re-applying them tick by tick with no input gives the same world.
    Scene second(3);
    const auto& entries = first.log().entries;
    std::size_t next = 0;
    for (std::uint64_t tick = 0; tick < first.tickCount(); ++tick) {
        while (next < entries.size() && entries[next].tick == tick) {
            second.enqueue(entries[next].command);
            ++next;
        }
        second.tick(kDt);
    }
    REQUIRE(second.stateHash() == expected);
}

TEST_CASE("save and load round-trip")
{
    const InputStream inputs = randomInputs(99, 200);
    Scene scene(21);
    populate(scene);
    scene.rng("loot").nextU32();
    scene.rng("weather").nextU64();
    runWithInputs(scene, inputs);
    const std::uint64_t before = scene.stateHash();

    const auto path = std::filesystem::temp_directory_path() / "ichor_roundtrip.isav";
    REQUIRE(saveScene(path, scene));

    Scene loaded(21);
    loaded.enqueue(piece("stale", 77, {5.0, 5.0, 5.0}));
    loaded.tick(kDt);
    REQUIRE(loadScene(path, loaded));
    std::filesystem::remove(path);

    REQUIRE(loaded.stateHash() == before);
    REQUIRE((loaded.find(StableId::of("stale", 77)) == entt::null));
    REQUIRE(loaded.tickCount() == scene.tickCount());
    REQUIRE(loaded.rng("loot").state() == scene.rng("loot").state());

    // Both continue identically from the snapshot.
    const InputStream more = randomInputs(100, 50);
    REQUIRE(runWithInputs(loaded, more) == runWithInputs(scene, more));
}

TEST_CASE("a snapshot from another seed or a truncated file is refused")
{
    Scene scene(1);
    populate(scene);
    scene.tick(kDt);
    const auto path = std::filesystem::temp_directory_path() / "ichor_refused.isav";
    REQUIRE(saveScene(path, scene));

    Scene wrongSeed(2);
    REQUIRE_FALSE(loadScene(path, wrongSeed));

    const auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size - 7);
    Scene target(1);
    target.enqueue(player());
    target.tick(kDt);
    const std::uint64_t untouched = target.stateHash();
    REQUIRE_FALSE(loadScene(path, target));
    REQUIRE(target.stateHash() == untouched);
    std::filesystem::remove(path);
}

TEST_CASE("per-system generators are independent and seeded by name")
{
    Scene a(4);
    Scene b(4);
    const auto x = a.rng("combat").nextU32();
    b.rng("weather").nextU32();
    REQUIRE(b.rng("combat").nextU32() == x);
    REQUIRE(a.rng("weather").nextU32() != x);
}
