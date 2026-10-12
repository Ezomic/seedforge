#include "ichor/editor/session.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <cmath>
#include <limits>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr double kDt = 1.0 / 60.0;

bool inScene(const Scene& scene, StableId id)
{
    return scene.find(id) != entt::entity{entt::null};
}

SpawnCommand wall(const char* name, glm::dvec3 position)
{
    SpawnCommand spawn;
    spawn.id = StableId::of(name);
    spawn.name.value = name;
    spawn.transform.position = position;
    spawn.staticBox = StaticBox{{2.0f, 1.0f, 0.5f}};
    spawn.mesh = MeshRef{3};
    spawn.spin = Spin{{0, 1, 0}, 0.5f};
    return spawn;
}

}

TEST_CASE("an edit is visible to the session at once and to the scene after a tick")
{
    Scene scene(1);
    EditorSession editor(scene);
    const auto id = editor.spawn("altar", {1.0, 0.0, 2.0}, MeshRef{4});
    REQUIRE(id.has_value());
    REQUIRE(editor.exists(*id));
    REQUIRE_FALSE(inScene(scene, *id)); // commands apply at the start of the tick
    REQUIRE(editor.outliner().size() == 1);
    REQUIRE(editor.outliner()[0].name == "altar");

    scene.tick(kDt);
    REQUIRE(inScene(scene, *id));
    REQUIRE(scene.registry().get<MeshRef>(scene.find(*id)).mesh == 4);
    REQUIRE(editor.exists(*id));
    REQUIRE(editor.outliner().size() == 1);
}

TEST_CASE("a name that is empty or already taken is refused")
{
    Scene scene(1);
    EditorSession editor(scene);
    REQUIRE_FALSE(editor.spawn("", {0, 0, 0}).has_value());
    REQUIRE(editor.spawn("well", {0, 0, 0}).has_value());
    REQUIRE_FALSE(editor.spawn("well", {5, 0, 0}).has_value()); // taken, even before the tick
    scene.tick(kDt);
    REQUIRE_FALSE(editor.spawn("well", {5, 0, 0}).has_value());
    REQUIRE(editor.historySize() == 1);
}

TEST_CASE("undo and redo walk a spawn back and forth")
{
    Scene scene(1);
    EditorSession editor(scene);
    const StableId id = *editor.spawn("altar", {1, 0, 2});
    scene.tick(kDt);
    REQUIRE(inScene(scene, id));

    REQUIRE(editor.canUndo());
    REQUIRE(editor.undo());
    scene.tick(kDt);
    REQUIRE_FALSE(inScene(scene, id));
    REQUIRE_FALSE(editor.exists(id));
    REQUIRE(editor.canRedo());

    REQUIRE(editor.redo());
    scene.tick(kDt);
    REQUIRE(inScene(scene, id));
    REQUIRE(scene.registry().get<Transform>(scene.find(id)).position == glm::dvec3(1, 0, 2));
}

TEST_CASE("undoing a destroy brings back everything the entity was")
{
    Scene scene(1);
    scene.enqueue(wall("gate", {10, 0, 0}));
    scene.tick(kDt);
    EditorSession editor(scene);
    const StableId id = StableId::of("gate");

    REQUIRE(editor.destroy(id));
    scene.tick(kDt);
    REQUIRE_FALSE(inScene(scene, id));
    REQUIRE_FALSE(scene.physics().hasBody(id));

    REQUIRE(editor.undo());
    scene.tick(kDt);
    REQUIRE(inScene(scene, id));
    const entt::entity entity = scene.find(id);
    REQUIRE(scene.registry().get<Name>(entity).value == "gate");
    REQUIRE(scene.registry().get<Transform>(entity).position == glm::dvec3(10, 0, 0));
    REQUIRE(scene.registry().get<MeshRef>(entity).mesh == 3);
    REQUIRE(scene.registry().get<Spin>(entity).radiansPerSecond == 0.5f);
    REQUIRE(scene.registry().get<StaticBox>(entity).halfExtents == glm::vec3(2.0f, 1.0f, 0.5f));
    REQUIRE(scene.physics().hasBody(id));
}

TEST_CASE("a transform edit moves the entity and its collider, and undo puts both back")
{
    Scene scene(1);
    scene.enqueue(wall("gate", {10, 0, 0}));
    scene.tick(kDt);
    EditorSession editor(scene);
    const StableId id = StableId::of("gate");

    Transform moved;
    moved.position = {20, 3, -4};
    moved.scale = glm::vec3(2.0f);
    REQUIRE(editor.setTransform(id, moved));
    scene.tick(kDt);
    REQUIRE(scene.registry().get<Transform>(scene.find(id)).position == glm::dvec3(20, 3, -4));
    REQUIRE(scene.registry().get<Transform>(scene.find(id)).scale == glm::vec3(2.0f));
    REQUIRE(scene.physics().hasBody(id));

    REQUIRE(editor.undo());
    scene.tick(kDt);
    REQUIRE(scene.registry().get<Transform>(scene.find(id)).position == glm::dvec3(10, 0, 0));
    REQUIRE(scene.physics().hasBody(id));
    // Nothing is left to interpolate across.
    REQUIRE(scene.registry().get<PreviousTransform>(scene.find(id)).position == glm::dvec3(10, 0, 0));
}

TEST_CASE("several edits in a tick are undone in the right order")
{
    Scene scene(1);
    EditorSession editor(scene);
    const StableId id = *editor.spawn("altar", {0, 0, 0});
    Transform a;
    a.position = {1, 0, 0};
    Transform b;
    b.position = {2, 0, 0};
    REQUIRE(editor.setTransform(id, a)); // all before any tick
    REQUIRE(editor.setTransform(id, b));
    scene.tick(kDt);
    REQUIRE(scene.registry().get<Transform>(scene.find(id)).position.x == 2.0);

    REQUIRE(editor.undo());
    scene.tick(kDt);
    REQUIRE(scene.registry().get<Transform>(scene.find(id)).position.x == 1.0);
    REQUIRE(editor.undo());
    scene.tick(kDt);
    REQUIRE(scene.registry().get<Transform>(scene.find(id)).position.x == 0.0);
    REQUIRE(editor.undo());
    scene.tick(kDt);
    REQUIRE_FALSE(inScene(scene, id));
    REQUIRE_FALSE(editor.canUndo());
}

TEST_CASE("a transaction is one step however many edits it holds")
{
    Scene scene(1);
    EditorSession editor(scene);
    const StableId id = *editor.spawn("altar", {0, 0, 0});
    scene.tick(kDt);
    const std::size_t before = editor.historySize();

    editor.beginTransaction();
    for (int i = 1; i <= 10; ++i) {
        Transform t;
        t.position = {static_cast<double>(i), 0, 0};
        REQUIRE(editor.setTransform(id, t));
        scene.tick(kDt);
    }
    REQUIRE_FALSE(editor.undo()); // not while a drag is open
    editor.endTransaction();
    REQUIRE(editor.historySize() == before + 1);

    REQUIRE(editor.undo());
    scene.tick(kDt);
    REQUIRE(scene.registry().get<Transform>(scene.find(id)).position.x == 0.0);
    REQUIRE(editor.redo());
    scene.tick(kDt);
    REQUIRE(scene.registry().get<Transform>(scene.find(id)).position.x == 10.0);

    editor.beginTransaction();
    editor.endTransaction(); // an empty drag is nothing
    REQUIRE(editor.historySize() == before + 1);
}

TEST_CASE("a new edit clears what could have been redone")
{
    Scene scene(1);
    EditorSession editor(scene);
    editor.spawn("one", {0, 0, 0});
    editor.spawn("two", {1, 0, 0});
    scene.tick(kDt);
    REQUIRE(editor.undo());
    REQUIRE(editor.canRedo());
    editor.spawn("three", {2, 0, 0});
    REQUIRE_FALSE(editor.canRedo());
    scene.tick(kDt);
    REQUIRE(editor.outliner().size() == 2);
}

TEST_CASE("history is bounded and drops the oldest step")
{
    Scene scene(1);
    EditorSession editor(scene, 5);
    for (int i = 0; i < 12; ++i) {
        editor.spawn("tree" + std::to_string(i), {static_cast<double>(i), 0, 0});
    }
    scene.tick(kDt);
    REQUIRE(editor.historySize() == 5);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(editor.undo());
    }
    REQUIRE_FALSE(editor.undo());
    scene.tick(kDt);
    REQUIRE(editor.outliner().size() == 7); // the seven oldest spawns stay
}

TEST_CASE("selection follows existence and the outliner is ordered by name")
{
    Scene scene(1);
    EditorSession editor(scene);
    const StableId b = *editor.spawn("beta", {0, 0, 0});
    const StableId a = *editor.spawn("alpha", {1, 0, 0});
    scene.tick(kDt);

    editor.select(b);
    REQUIRE(editor.selection() == b);
    const auto list = editor.outliner();
    REQUIRE(list.size() == 2);
    REQUIRE(list[0].name == "alpha");
    REQUIRE_FALSE(list[0].selected);
    REQUIRE(list[1].selected);

    editor.select(StableId::of("ghost")); // nothing to select
    REQUIRE_FALSE(editor.selection().has_value());

    editor.select(a);
    REQUIRE(editor.destroy(a));
    REQUIRE_FALSE(editor.selection().has_value());
}

TEST_CASE("players and characters belong to the game, not the editor")
{
    Scene scene(1);
    SpawnCommand hero;
    hero.id = StableId::of("hero");
    hero.name.value = "hero";
    hero.player = Player{};
    hero.character = CharacterBody{};
    hero.transform.position = {0, 1, 0};
    scene.enqueue(hero);
    scene.tick(kDt);

    EditorSession editor(scene);
    REQUIRE_FALSE(editor.destroy(hero.id));
    REQUIRE_FALSE(editor.setTransform(hero.id, Transform{}));
    REQUIRE(editor.historySize() == 0);
    scene.tick(kDt);
    REQUIRE(inScene(scene, hero.id));
}

TEST_CASE("editor changes enter the command log and replay to the same world")
{
    Scene live(5);
    EditorSession editor(live);
    const StableId id = *editor.spawn("altar", {1, 0, 2}, MeshRef{2});
    live.tick(kDt);
    Transform moved;
    moved.position = {9, 0, 9};
    editor.setTransform(id, moved);
    live.tick(kDt);

    // Each logged command is stamped with the tick it applied at; feeding them back tick by tick
    // into a fresh scene gives the same world.
    Scene replay(5);
    const auto& entries = live.log().entries;
    REQUIRE(entries.size() == 2);
    std::size_t next = 0;
    for (std::uint64_t tick = 0; tick < live.tickCount(); ++tick) {
        while (next < entries.size() && entries[next].tick == tick) {
            replay.enqueue(entries[next].command);
            ++next;
        }
        replay.tick(kDt);
    }
    REQUIRE(replay.stateHash() == live.stateHash());
}

TEST_CASE("reset forgets the history, the selection and what was queued")
{
    Scene scene(1);
    EditorSession editor(scene);
    const StableId id = *editor.spawn("altar", {0, 0, 0});
    editor.select(id);
    REQUIRE(editor.historySize() == 1);
    editor.reset();
    REQUIRE(editor.historySize() == 0);
    REQUIRE_FALSE(editor.canRedo());
    REQUIRE_FALSE(editor.selection().has_value());
    REQUIRE_FALSE(editor.exists(id)); // the spawn was never applied, and the session no longer claims it
    REQUIRE(editor.outliner().empty());
}

TEST_CASE("the selection goes when something else destroys the entity")
{
    Scene scene(1);
    scene.enqueue(wall("gate", {0, 0, 0}));
    scene.tick(kDt);
    EditorSession editor(scene);
    const StableId id = StableId::of("gate");
    editor.select(id);
    REQUIRE(editor.selection() == id);
    scene.enqueue(DestroyCommand{id});
    scene.tick(kDt);
    REQUIRE_FALSE(editor.selection().has_value());
}

TEST_CASE("an undo that would spawn an id the game has taken again is refused and dropped")
{
    Scene scene(1);
    scene.enqueue(wall("gate", {0, 0, 0}));
    scene.tick(kDt);
    EditorSession editor(scene);
    const StableId id = StableId::of("gate");
    REQUIRE(editor.destroy(id));
    scene.tick(kDt);
    scene.enqueue(wall("gate", {5, 0, 0})); // a game system puts one back
    scene.tick(kDt);
    REQUIRE(editor.canUndo());
    REQUIRE_FALSE(editor.undo());
    REQUIRE_FALSE(editor.canUndo());
    REQUIRE(scene.registry().get<Transform>(scene.find(id)).position.x == 5.0);
}

TEST_CASE("undoing a move on a spinning entity does not snap its rotation back")
{
    Scene scene(1);
    scene.enqueue(wall("gate", {0, 0, 0}));
    scene.tick(kDt);
    EditorSession editor(scene);
    const StableId id = StableId::of("gate");

    Transform moved = *editor.transformOf(id);
    moved.position.x = 7.0;
    REQUIRE(editor.setTransform(id, moved));
    for (int i = 0; i < 30; ++i) {
        scene.tick(kDt);
    }
    const glm::quat turned = scene.registry().get<Transform>(scene.find(id)).rotation;
    REQUIRE(editor.undo());
    scene.tick(kDt);
    const Transform& after = scene.registry().get<Transform>(scene.find(id));
    REQUIRE(after.position.x == 0.0);
    // The rotation carried on from where the spin had got to, a tick further on.
    REQUIRE(glm::dot(after.rotation, turned) > 0.999f);
}

TEST_CASE("a rotation is normalised and a broken transform is refused")
{
    Scene scene(1);
    EditorSession editor(scene);
    const StableId id = *editor.spawn("altar", {0, 0, 0});
    scene.tick(kDt);

    Transform skewed;
    skewed.rotation = glm::quat(3.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(editor.setTransform(id, skewed));
    scene.tick(kDt);
    REQUIRE(glm::length(scene.registry().get<Transform>(scene.find(id)).rotation) == Approx(1.0f).margin(1e-5));

    Transform zero;
    zero.rotation = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE_FALSE(editor.setTransform(id, zero));
    Transform nan;
    nan.position.x = std::nan("");
    REQUIRE_FALSE(editor.setTransform(id, nan));
    Transform inf;
    inf.scale.y = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(editor.setTransform(id, inf));
}

TEST_CASE("transactions nest, and only the outermost end closes the step")
{
    Scene scene(1);
    EditorSession editor(scene);
    const StableId id = *editor.spawn("altar", {0, 0, 0});
    scene.tick(kDt);
    const std::size_t before = editor.historySize();

    editor.beginTransaction();
    editor.beginTransaction();
    Transform a;
    a.position.x = 1.0;
    editor.setTransform(id, a);
    editor.endTransaction(); // the inner one: still open
    REQUIRE_FALSE(editor.canUndo());
    Transform b;
    b.position.x = 2.0;
    editor.setTransform(id, b);
    editor.endTransaction();
    REQUIRE(editor.historySize() == before + 1);
    REQUIRE(editor.canUndo());
}
