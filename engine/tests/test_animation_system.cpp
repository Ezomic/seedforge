#include "ichor/anim/animation.h"
#include "ichor/assets/bundle.h"
#include "ichor/assets/cooker.h"
#include "ichor/sim/scene.h"
#include "ichor/sim/scene_io.h"
#include "test_temp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;

struct Fixture {
    SceneBundle bundle;
    AnimationSet animations;
    Fixture()
    {
        CookStats stats;
        REQUIRE(cookGltf(std::filesystem::path(ICHOR_TEST_DATA) / "arm.glb", bundle, stats));
        REQUIRE(animations.load(bundle));
    }
};

SpawnCommand armSpawn(bool rootMotion, bool character)
{
    SpawnCommand spawn;
    spawn.id = StableId::of("arm");
    spawn.name.value = "arm";
    spawn.mesh = MeshRef{0};
    spawn.animator = Animator{0, 0.0f, 1.0f, true, rootMotion};
    if (character) {
        spawn.transform.position = {0.0, 0.0, 0.0};
        spawn.character = CharacterBody{0.12f, 0.3f};
    }
    return spawn;
}

SpawnCommand floorSpawn()
{
    SpawnCommand spawn;
    spawn.id = StableId::of("floor");
    spawn.name.value = "floor";
    spawn.transform.position = {0.0, -0.5, 0.0};
    spawn.staticBox = StaticBox{glm::vec3(20.0f, 0.5f, 20.0f)};
    return spawn;
}

}

TEST_CASE("an animator advances, loops and fills the skin pose")
{
    Fixture f;
    Scene scene(1);
    scene.setAnimations(&f.animations);
    scene.enqueue(armSpawn(false, false));
    scene.tick(kDt);
    const entt::entity e = scene.find(StableId::of("arm"));
    REQUIRE(scene.registry().get<Animator>(e).time == Catch::Approx(kDt));
    REQUIRE(scene.registry().get<SkinPose>(e).matrices.size() == 2);

    const float duration = f.animations.clipDuration(0);
    for (int i = 0; i < 100; ++i) {
        scene.tick(kDt);
    }
    const float time = scene.registry().get<Animator>(e).time;
    REQUIRE(time >= 0.0f);
    REQUIRE(time < duration);
    REQUIRE(std::abs(time - std::fmod(101.0f * static_cast<float>(kDt), duration)) < 1e-3f);
}

TEST_CASE("root motion walks a character about one metre per clip")
{
    Fixture f;
    Scene scene(2);
    scene.setAnimations(&f.animations);
    scene.enqueue(floorSpawn());
    scene.enqueue(armSpawn(true, true));
    for (int i = 0; i < 30; ++i) {
        scene.tick(kDt); // settle
    }
    const glm::dvec3 start = scene.registry().get<Transform>(scene.find(StableId::of("arm"))).position;
    const int ticks = static_cast<int>(std::round(f.animations.clipDuration(0) / kDt));
    for (int i = 0; i < ticks; ++i) {
        scene.tick(kDt);
    }
    const glm::dvec3 end = scene.registry().get<Transform>(scene.find(StableId::of("arm"))).position;
    const glm::dvec3 travel = end - start;
    REQUIRE(std::abs(glm::length(glm::dvec2(travel.x, travel.z)) - 1.0) < 0.15);

    // The travel went into the transform, so the pose keeps its root at home.
    const auto& pose = scene.registry().get<SkinPose>(scene.find(StableId::of("arm"))).matrices;
    REQUIRE(pose.size() == 2);
    REQUIRE(glm::length(glm::vec3(pose[0][3])) < 0.05f);
}

TEST_CASE("a montage commits: a second play is ignored until recovery ends")
{
    Fixture f;
    Scene scene(3);
    scene.setAnimations(&f.animations);
    scene.enqueue(armSpawn(false, false));
    scene.tick(kDt);
    PlayMontageCommand light{StableId::of("arm"), 0, 0.35f, 0.5f, 0.9f};
    scene.enqueue(light);
    scene.tick(kDt);
    const entt::entity e = scene.find(StableId::of("arm"));
    REQUIRE(scene.registry().get<MontagePlayer>(e).phase == static_cast<std::uint8_t>(MontagePhase::Windup));

    PlayMontageCommand heavy{StableId::of("arm"), 0, 0.7f, 0.9f, 1.7f};
    scene.enqueue(heavy);
    scene.tick(kDt);
    REQUIRE(scene.registry().get<MontagePlayer>(e).activeStart == 0.35f);

    // The command tick counts as the first advance, the ignored second play as the second.
    int activeTick = -1;
    for (int tick = 3; tick <= 60; ++tick) {
        scene.tick(kDt);
        if (activeTick < 0 && scene.registry().get<MontagePlayer>(e).phase == static_cast<std::uint8_t>(MontagePhase::Active)) {
            activeTick = tick;
        }
    }
    REQUIRE(activeTick == 21);
    REQUIRE(scene.registry().get<MontagePlayer>(e).phase == 0);
    scene.enqueue(heavy);
    scene.tick(kDt);
    REQUIRE(scene.registry().get<MontagePlayer>(e).activeStart == 0.7f);
}

TEST_CASE("animator and montage state survive a snapshot and hash")
{
    Fixture f;
    Scene scene(4);
    scene.setAnimations(&f.animations);
    scene.enqueue(armSpawn(false, false));
    scene.tick(kDt);
    scene.enqueue(PlayMontageCommand{StableId::of("arm"), 0, 0.35f, 0.5f, 0.9f});
    for (int i = 0; i < 10; ++i) {
        scene.tick(kDt);
    }
    const auto path = test::tempPath("ichor_anim.isav");
    REQUIRE(saveScene(path, scene));
    Scene loaded(4);
    loaded.setAnimations(&f.animations);
    REQUIRE(loadScene(path, loaded));
    std::filesystem::remove(path);
    REQUIRE(loaded.stateHash() == scene.stateHash());
    const entt::entity e = loaded.find(StableId::of("arm"));
    REQUIRE(loaded.registry().all_of<MontagePlayer>(e));
    REQUIRE(loaded.registry().get<MontagePlayer>(e).time == scene.registry().get<MontagePlayer>(scene.find(StableId::of("arm"))).time);
    for (int i = 0; i < 20; ++i) {
        scene.tick(kDt);
        loaded.tick(kDt);
    }
    REQUIRE(loaded.stateHash() == scene.stateHash());
}
