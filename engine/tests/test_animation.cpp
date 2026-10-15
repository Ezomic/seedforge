#include "ichor/anim/animation.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/cooker.h"
#include "test_temp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>

using namespace ichor;

namespace {

const std::filesystem::path kArm = std::filesystem::path(ICHOR_TEST_DATA) / "arm.glb";

SceneBundle cookArm()
{
    SceneBundle bundle;
    CookStats stats;
    REQUIRE(cookGltf(kArm, bundle, stats));
    return bundle;
}

}

TEST_CASE("the arm cooks to one skeleton, one skinned mesh and one clip")
{
    const SceneBundle bundle = cookArm();
    REQUIRE(bundle.skeletons.size() == 1);
    REQUIRE(bundle.skeletons[0].jointNames == std::vector<std::string>{"root", "tip"});
    REQUIRE(bundle.skeletons[0].inverseBind.size() == 2);
    REQUIRE(bundle.clips.size() == 1);
    REQUIRE(bundle.clips[0].name == "wave");
    REQUIRE(bundle.clips[0].duration == Catch::Approx(1.0f).margin(0.03f));
    REQUIRE(bundle.meshes.size() == 1);
    REQUIRE(bundle.meshes[0].skeleton == 0);

    bool anyTip = false;
    for (const Vertex& v : bundle.meshes[0].vertices) {
        std::uint32_t sum = 0;
        for (std::uint16_t w : v.weights) sum += w;
        REQUIRE(sum >= 65534);
        REQUIRE(sum <= 65536);
        anyTip = anyTip || (v.joints[1] == 1 && v.weights[1] > 0);
    }
    REQUIRE(anyTip);

    const auto path = test::tempPath("ichor_arm.iscene");
    REQUIRE(writeBundle(path, bundle));
    SceneBundle loaded;
    REQUIRE(readBundle(path, loaded));
    std::filesystem::remove(path);
    REQUIRE(loaded.skeletons[0].ozzBytes == bundle.skeletons[0].ozzBytes);
    REQUIRE(loaded.clips[0].ozzBytes == bundle.clips[0].ozzBytes);
    REQUIRE(loaded.meshes[0].vertices.size() == bundle.meshes[0].vertices.size());
}

TEST_CASE("sampling the wave bends the tip joint as authored")
{
    const SceneBundle bundle = cookArm();
    AnimationSet set;
    REQUIRE(set.load(bundle));
    REQUIRE(set.skeletonCount() == 1);
    REQUIRE(set.jointCount(0) == 2);
    const std::uint32_t clip = set.findClip("wave");
    REQUIRE(clip != AnimationSet::kNoClip);

    // The tip is keyed to 60 degrees about X at frame 16 of 61. Exporters
    // place frame zero differently, so find the peak rather than assume its time.
    std::vector<glm::mat4> pose;
    float peakAngle = 0.0f;
    float peakTime = 0.0f;
    // Only the first half: the return swing to minus 60 is the same size.
    for (int i = 0; i <= 100; ++i) {
        const float t = set.clipDuration(clip) * 0.5f * static_cast<float>(i) / 100.0f;
        REQUIRE(set.samplePose(clip, t, pose));
        REQUIRE(pose.size() == 2);
        const float angle = glm::degrees(glm::angle(glm::quat_cast(glm::mat3(pose[1]))));
        if (angle > peakAngle) {
            peakAngle = angle;
            peakTime = t;
        }
    }
    REQUIRE(std::abs(peakAngle - 60.0f) < 1.0f);
    REQUIRE(std::abs(peakTime - 0.25f) < 0.04f);
    REQUIRE(set.samplePose(clip, 0.25f, pose));

    // At rest the tip sits 0.5 m up the root; the pose keeps it there.
    REQUIRE(set.samplePose(clip, 0.0f, pose));
    REQUIRE(glm::length(glm::vec3(pose[1][3]) - glm::vec3(0.0f, 0.5f, 0.0f)) < 0.01f);

    // Pure: the same time gives the same matrices.
    std::vector<glm::mat4> first;
    std::vector<glm::mat4> again;
    REQUIRE(set.samplePose(clip, 0.25f, first));
    REQUIRE(set.samplePose(clip, 0.25f, again));
    REQUIRE(again == first);
}

TEST_CASE("skinning matrices leave bind-pose vertices in place at time zero")
{
    const SceneBundle bundle = cookArm();
    AnimationSet set;
    REQUIRE(set.load(bundle));
    std::vector<glm::mat4> skin;
    REQUIRE(set.skinningMatrices(0, 0.0f, skin));
    for (const glm::mat4& m : skin) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                REQUIRE(std::abs(m[c][r] - (c == r ? 1.0f : 0.0f)) < 1e-3f);
            }
        }
    }
}

TEST_CASE("root motion: the root walks one metre over the clip")
{
    const SceneBundle bundle = cookArm();
    AnimationSet set;
    REQUIRE(set.load(bundle));
    glm::vec3 start;
    glm::vec3 end;
    glm::vec3 middle;
    REQUIRE(set.rootTranslation(0, 0.0f, start));
    REQUIRE(set.rootTranslation(0, set.clipDuration(0), end));
    REQUIRE(set.rootTranslation(0, set.clipDuration(0) * 0.5f, middle));
    const glm::vec3 delta = end - start;
    REQUIRE(glm::length(delta) == Catch::Approx(1.0f).margin(0.03f));
    REQUIRE(glm::length((middle - start) - delta * 0.5f) < 0.03f);
}

TEST_CASE("montage phases change on exact ticks")
{
    // Doc 18 light attack: 0.35 s wind-up, 0.15 s active, 0.4 s recovery, at 60 Hz.
    Montage light;
    light.clip = 0;
    light.activeStart = 0.35f;
    light.activeEnd = 0.5f;
    light.recoveryEnd = 0.9f;

    MontageState state;
    REQUIRE_FALSE(state.busy());
    state.start(light);
    REQUIRE(state.phase == MontagePhase::Windup);

    const float dt = 1.0f / 60.0f;
    int tick = 0;
    int activeTick = -1;
    int recoveryTick = -1;
    int idleTick = -1;
    while (state.busy() && tick < 120) {
        const MontagePhase entered = state.advance(dt);
        ++tick;
        if (entered == MontagePhase::Active) activeTick = tick;
        if (entered == MontagePhase::Recovery) recoveryTick = tick;
        if (entered == MontagePhase::Idle && !state.busy()) idleTick = tick;
    }
    REQUIRE(activeTick == 21);
    REQUIRE(recoveryTick == 30);
    REQUIRE(idleTick == 54);
    REQUIRE_FALSE(state.busy());
    REQUIRE(state.advance(dt) == MontagePhase::Idle);
}
