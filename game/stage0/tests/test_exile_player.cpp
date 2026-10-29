#include "ichor/anim/animation.h"
#include "ichor/assets/cooker.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/sim/scene.h"
#include "ichor/stage0/exile_player.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <filesystem>
#include <limits>

using namespace ichor;
using namespace ichor::stage0;

namespace {

constexpr float kDt = 1.0f / 60.0f;
const std::filesystem::path kExile = std::filesystem::path(ICHOR_ART_DIR) / "exile";

Combatant in(CombatState state, float time, AttackKind attack = AttackKind::Light)
{
    Combatant c;
    c.state = static_cast<std::uint8_t>(state);
    c.attack = static_cast<std::uint8_t>(attack);
    c.stateTime = time;
    return c;
}

ExilePlayer& exile()
{
    static NullRenderer renderer;
    static ExilePlayer player;
    static const bool ok = player.load(kExile, renderer);
    REQUIRE(ok);
    return player;
}

// Where the spear's grip is, in the character's frame (x right, y up, forward -Z), for a clip choice.
glm::vec3 gripAt(const ExileClipChoice& choice)
{
    RenderView view;
    std::vector<glm::mat4> skin;
    REQUIRE(exile().pose(choice, 0.0f, skin));
    exile().appendPosed(skin, glm::dvec3(0.0), 0.0f, view);
    REQUIRE(view.instances.size() == 3);
    return glm::vec3(view.instances[1].position);
}

}

TEST_CASE("each combat state picks its clip")
{
    const Tuning tuning;
    CHECK(chooseExileClip(in(CombatState::Windup, 0.1f), tuning, 0.0f, kDt).clip == ExileClip::Thrust);
    CHECK(chooseExileClip(in(CombatState::Active, 0.05f), tuning, 0.0f, kDt).clip == ExileClip::Thrust);
    CHECK(chooseExileClip(in(CombatState::Recovery, 0.1f, AttackKind::Heavy), tuning, 0.0f, kDt).clip == ExileClip::Swing);
    CHECK(chooseExileClip(in(CombatState::Dodging, 0.1f), tuning, 0.0f, kDt).clip == ExileClip::Dodge);
    CHECK(chooseExileClip(in(CombatState::Staggered, 0.1f), tuning, 0.0f, kDt).clip == ExileClip::Hit);
    CHECK(chooseExileClip(in(CombatState::Dead, 0.1f), tuning, 0.0f, kDt).clip == ExileClip::Death);
    Combatant guard = in(CombatState::Idle, 0.0f);
    guard.blocking = true;
    CHECK(chooseExileClip(guard, tuning, 0.0f, kDt).clip == ExileClip::Block);
}

TEST_CASE("locomotion follows the ground speed")
{
    const Tuning tuning;
    const Combatant idle = in(CombatState::Idle, 0.0f);
    const auto at = [&](float speed) { return chooseExileClip(idle, tuning, speed, kDt); };
    CHECK(at(0.0f).clip == ExileClip::Idle);
    CHECK(at(1.0f).clip == ExileClip::Walk);
    CHECK(at(4.0f).clip == ExileClip::Run);
    CHECK(at(4.0f).locomotion);
    CHECK_FALSE(chooseExileClip(in(CombatState::Windup, 0.0f), tuning, 4.0f, kDt).locomotion);
    CHECK(at(std::numeric_limits<float>::quiet_NaN()).clip == ExileClip::Idle);
}

TEST_CASE("an attack's clip is stretched over wind-up, active and recovery and ends with the state")
{
    const Tuning tuning;
    const float total = tuning.lightWindup + tuning.lightActive + tuning.lightRecovery;
    CHECK(chooseExileClip(in(CombatState::Windup, 0.0f), tuning, 0.0f, kDt).progress == Catch::Approx(0.0f));
    CHECK(chooseExileClip(in(CombatState::Active, 0.0f), tuning, 0.0f, kDt).progress == Catch::Approx(tuning.lightWindup / total));
    CHECK(chooseExileClip(in(CombatState::Recovery, 0.0f), tuning, 0.0f, kDt).progress == Catch::Approx((tuning.lightWindup + tuning.lightActive) / total));
    CHECK(chooseExileClip(in(CombatState::Recovery, tuning.lightRecovery), tuning, 0.0f, kDt).progress == Catch::Approx(1.0f));
    CHECK(chooseExileClip(in(CombatState::Dodging, 99.0f), tuning, 0.0f, kDt).progress == 1.0f);
    CHECK(chooseExileClip(in(CombatState::Dead, std::numeric_limits<float>::infinity()), tuning, 0.0f, kDt).progress == 1.0f);
    CHECK(chooseExileClip(in(CombatState::Dead, std::numeric_limits<float>::quiet_NaN()), tuning, 0.0f, kDt).progress == 0.0f);
}

TEST_CASE("the guard comes up over a short time and then holds")
{
    const Tuning tuning;
    Combatant guard = in(CombatState::Idle, 0.0f);
    guard.blocking = true;
    guard.blockTicks = 0;
    CHECK(chooseExileClip(guard, tuning, 0.0f, kDt).progress == 0.0f);
    guard.blockTicks = 600;
    CHECK(chooseExileClip(guard, tuning, 0.0f, kDt).progress == Catch::Approx(kExileGuardPhase));
    guard.blockTicks = -5;
    CHECK(chooseExileClip(guard, tuning, 0.0f, kDt).progress == 0.0f);
}

TEST_CASE("the playback rate keeps the feet on the ground within a range")
{
    CHECK(exileLocomotionRate(5.2f, 5.2f) == Catch::Approx(1.0f));
    CHECK(exileLocomotionRate(2.6f, 5.2f) == Catch::Approx(0.5f));
    CHECK(exileLocomotionRate(0.0f, 5.2f) == Catch::Approx(kExileMinRate));
    CHECK(exileLocomotionRate(99.0f, 5.2f) == Catch::Approx(kExileMaxRate));
    CHECK(exileLocomotionRate(4.0f, 0.0f) == 1.0f);
    CHECK(exileLocomotionRate(std::numeric_limits<float>::quiet_NaN(), 5.2f) == 1.0f);
}

TEST_CASE("the Exile loads with all nine clips and draws a body, a spear and a shield")
{
    NullRenderer renderer;
    ExilePlayer player;
    REQUIRE(player.load(kExile, renderer));
    std::vector<glm::mat4> skin;
    REQUIRE(player.pose({}, 0.0f, skin));
    RenderView view;
    player.appendPosed(skin, glm::dvec3(1.0, 0.0, 2.0), 0.0f, view);
    REQUIRE(view.instances.size() == 3);
    CHECK(view.instances[0].jointCount == skin.size());
    CHECK(view.jointPalette.size() == skin.size());
    CHECK(view.instances[1].jointCount == 0);
    CHECK(view.instances[2].jointCount == 0);
    CHECK_FALSE(player.load(kExile / "missing", renderer));
}

TEST_CASE("the character stays in place: the root travel of a locomotion clip is taken out of the pose")
{
    ExilePlayer& player = exile();
    ExileClipChoice run;
    run.clip = ExileClip::Run;
    run.locomotion = true;
    std::vector<glm::mat4> first;
    std::vector<glm::mat4> later;
    REQUIRE(player.pose(run, 0.0f, first));
    REQUIRE(player.pose(run, 0.9f, later));
    // The root joint carries the clip's travel (about a metre over Run); with it taken out the root stays put.
    const float drift = std::hypot(later[0][3].x - first[0][3].x, later[0][3].z - first[0][3].z);
    CHECK(drift < 0.01f);
}

TEST_CASE("a thrust drives the spear hand forward through the active window and a stand does not")
{
    const Tuning tuning;
    const float total = tuning.lightWindup + tuning.lightActive + tuning.lightRecovery;
    ExileClipChoice start;
    start.clip = ExileClip::Thrust;
    start.progress = 0.0f;
    ExileClipChoice struck = start;
    struck.progress = (tuning.lightWindup + tuning.lightActive * 0.5f) / total;
    ExileClipChoice idleA;
    ExileClipChoice idleB;
    idleB.progress = 0.0f;
    const glm::vec3 a = gripAt(start);
    const glm::vec3 b = gripAt(struck);
    CHECK(glm::distance(a, b) > 0.25f);
    CHECK(glm::distance(gripAt(idleA), gripAt(idleB)) < 1e-4f);
}

TEST_CASE("drawing the Exile does not touch the simulation")
{
    NullRenderer renderer;
    ExilePlayer player;
    REQUIRE(player.load(kExile, renderer));
    const Tuning tuning;
    const auto play = [&](bool draw) {
        Scene scene(1);
        Stage0 stage(tuning);
        stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
        scene.tick(1.0 / 60.0);
        stage.resetFight(scene);
        for (int i = 0; i < 240; ++i) {
            InputFrame input;
            input.buttons = i % 90 < 3 ? InputFrame::Attack : 0u;
            input.moveY = i % 120 < 60 ? 1.0f : 0.0f;
            scene.tick(1.0 / 60.0, input);
            if (draw) {
                RenderView view;
                player.append(scene, 0.5, 1.0 / 60.0, kDt, tuning, view);
                REQUIRE(view.instances.size() == 3);
            }
        }
        return scene.stateHash();
    };
    CHECK(play(true) == play(false));
}

namespace {

std::unique_ptr<AnimationSet> animationsOf(const SceneBundle& bundle)
{
    auto set = std::make_unique<AnimationSet>();
    REQUIRE(set->load(bundle));
    return set;
}

float travelSpeedOf(const AnimationSet& set, const char* clipName)
{
    const std::uint32_t clip = set.findClip(clipName);
    REQUIRE(clip != AnimationSet::kNoClip);
    const float duration = set.clipDuration(clip);
    glm::vec3 start;
    glm::vec3 end;
    REQUIRE(set.rootTranslation(clip, 0.0f, start));
    REQUIRE(set.rootTranslation(clip, duration, end));
    return std::hypot(end.x - start.x, end.z - start.z) / duration;
}

glm::vec3 facingOf(const RenderView& view)
{
    return view.instances[0].basis * glm::vec3(0.0f, 0.0f, -1.0f);
}

}

TEST_CASE("every clip the player can pick exists in the rig, whatever the combat state")
{
    const auto owned = animationsOf(exile().kit().bundles[0]);
    const AnimationSet& set = *owned;
    for (std::size_t i = 0; i < kExileClipCount; ++i) {
        const char* clip = name(static_cast<ExileClip>(i));
        INFO(clip);
        CHECK(set.findClip(clip) != AnimationSet::kNoClip);
    }
    const Tuning tuning;
    for (std::uint8_t state = 0; state <= static_cast<std::uint8_t>(CombatState::Dead); ++state) {
        for (const AttackKind attack : {AttackKind::Light, AttackKind::Heavy}) {
            for (const bool blocking : {false, true}) {
                for (const float speed : {0.0f, 1.0f, 3.0f, 9.0f}) {
                    Combatant c = in(static_cast<CombatState>(state), 0.2f, attack);
                    c.blocking = blocking;
                    const ExileClipChoice choice = chooseExileClip(c, tuning, speed, kDt);
                    CHECK(static_cast<std::size_t>(choice.clip) < kExileClipCount);
                    CHECK(set.findClip(name(choice.clip)) != AnimationSet::kNoClip);
                }
            }
        }
    }
}

TEST_CASE("the feet keep pace with the ground at every speed the player can move")
{
    const auto owned = animationsOf(exile().kit().bundles[0]);
    const AnimationSet& set = *owned;
    const float travel[] = {0.0f, travelSpeedOf(set, "walk"), travelSpeedOf(set, "run")};
    const Tuning tuning;
    const Combatant idle = in(CombatState::Idle, 0.0f);
    for (float speed = 0.3f; speed <= 8.0f; speed += 0.05f) {
        const ExileClipChoice choice = chooseExileClip(idle, tuning, speed, kDt);
        REQUIRE(choice.locomotion);
        const std::size_t index = static_cast<std::size_t>(choice.clip);
        REQUIRE((index == 1 || index == 2));
        const float feet = exileLocomotionRate(speed, travel[index]) * travel[index];
        INFO("speed " << speed << " clip " << name(choice.clip));
        CHECK(feet == Catch::Approx(speed).epsilon(0.03));
    }
    const float run = exileLocomotionRate(tuning.moveSpeed, travel[2]);
    CHECK(run > 0.6f);
    CHECK(run < 1.0f);
}

TEST_CASE("blending two poses keeps each end exact and the joints rigid in between")
{
    ExilePlayer& player = exile();
    ExileClipChoice from;
    from.clip = ExileClip::Idle;
    ExileClipChoice to;
    to.clip = ExileClip::Dodge;
    to.progress = 0.3f;
    std::vector<glm::mat4> a;
    std::vector<glm::mat4> b;
    REQUIRE(player.pose(from, 0.0f, a));
    REQUIRE(player.pose(to, 0.0f, b));
    std::vector<glm::mat4> out;
    blendSkin(a, b, 0.0f, out);
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(glm::distance(a[i][3], out[i][3]) < 1e-4f);
    blendSkin(a, b, 1.0f, out);
    for (std::size_t i = 0; i < b.size(); ++i) CHECK(glm::distance(b[i][3], out[i][3]) < 1e-4f);
    blendSkin(a, b, 0.5f, out);
    for (std::size_t i = 0; i < out.size(); ++i) {
        for (int k = 0; k < 3; ++k) CHECK(glm::length(glm::vec3(out[i][k])) == Catch::Approx(1.0f).margin(0.01f));
        CHECK(glm::dot(glm::cross(glm::vec3(out[i][0]), glm::vec3(out[i][1])), glm::vec3(out[i][2])) > 0.98f);
    }
}

TEST_CASE("the body faces the way it travels, and a dodge steps along the way it was aimed")
{
    NullRenderer renderer;
    ExilePlayer player;
    REQUIRE(player.load(kExile, renderer));
    const auto owned = animationsOf(player.kit().bundles[0]);
    const AnimationSet& set = *owned;
    const Tuning tuning;
    Scene scene(1);
    Stage0 stage(tuning);
    stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -9.0});
    scene.tick(1.0 / 60.0);
    stage.resetFight(scene);
    for (int i = 0; i < 30; ++i) scene.tick(1.0 / 60.0);
    const entt::entity entity = scene.find(stage.playerId());

    InputFrame back;
    back.moveY = -1.0f;
    glm::vec3 facing(0.0f);
    for (int i = 0; i < 60; ++i) {
        scene.tick(1.0 / 60.0, back);
        RenderView view;
        player.append(scene, 1.0, 1.0 / 60.0, kDt, tuning, view);
        facing = facingOf(view);
    }
    const auto& transform = scene.registry().get<Transform>(entity);
    const auto& previous = scene.registry().get<PreviousTransform>(entity);
    const glm::dvec3 step = glm::normalize(transform.position - previous.position);
    const Combatant& c = scene.registry().get<Combatant>(entity);
    REQUIRE(glm::dot(glm::vec3(step), glm::vec3(std::sin(c.yaw), 0.0f, -std::cos(c.yaw))) < -0.9f);
    CHECK(glm::dot(facing, glm::vec3(step)) > 0.95f);

    InputFrame dodge;
    dodge.moveX = 1.0f;
    dodge.buttons = InputFrame::Dodge;
    scene.tick(1.0 / 60.0, dodge);
    REQUIRE(scene.registry().get<Combatant>(entity).combatState() == CombatState::Dodging);
    RenderView view;
    player.append(scene, 1.0, 1.0 / 60.0, kDt, tuning, view);
    const std::uint32_t clip = set.findClip("dodge");
    glm::vec3 start;
    glm::vec3 end;
    REQUIRE(set.rootTranslation(clip, 0.0f, start));
    REQUIRE(set.rootTranslation(clip, set.clipDuration(clip), end));
    const glm::vec3 travel = view.instances[0].basis * glm::normalize(end - start);
    const glm::vec3 wanted = scene.registry().get<Combatant>(entity).dodgeDirection;
    CHECK(glm::dot(travel, wanted) > 0.99f);
}

TEST_CASE("the three Exile costumes share one skeleton, one set of clips and a skin that fits them")
{
    SceneBundle reference;
    CookStats stats;
    CookOptions options;
    options.rootYawDegrees = 180.0f;
    REQUIRE(cookGltf(kExile / "SK_Exile.glb", reference, stats, options));
    REQUIRE(reference.skeletons.size() == 1);
    const Skeleton& skeleton = reference.skeletons[0];
    const auto referenceSet = animationsOf(reference);
    for (const char* costume : {"SK_ExileFounder.glb", "SK_ExileRags.glb"}) {
        INFO(costume);
        SceneBundle bundle;
        REQUIRE(cookGltf(kExile / costume, bundle, stats, options));
        REQUIRE(bundle.skeletons.size() == 1);
        CHECK(bundle.skeletons[0].jointNames == skeleton.jointNames);
        REQUIRE(bundle.skeletons[0].inverseBind.size() == skeleton.inverseBind.size());
        for (std::size_t i = 0; i < skeleton.inverseBind.size(); ++i) {
            for (int c = 0; c < 4; ++c) CHECK(glm::distance(bundle.skeletons[0].inverseBind[i][c], skeleton.inverseBind[i][c]) < 1e-3f);
        }
        REQUIRE_FALSE(bundle.meshes.empty());
        for (const Mesh& mesh : bundle.meshes) {
            CHECK(mesh.skeleton == 0);
            for (const Vertex& v : mesh.vertices) {
                for (int k = 0; k < 4; ++k) REQUIRE(v.joints[k] < skeleton.jointNames.size());
            }
        }
        const auto set = animationsOf(bundle);
        CHECK(set->clipCount() == referenceSet->clipCount());
        CHECK(set->jointNames(0) == referenceSet->jointNames(0));
        for (std::size_t i = 0; i < kExileClipCount; ++i) {
            const char* clipName = name(static_cast<ExileClip>(i));
            INFO(clipName);
            const std::uint32_t clip = set->findClip(clipName);
            REQUIRE(clip != AnimationSet::kNoClip);
            CHECK(set->clipSkeleton(clip) == 0);
            CHECK(set->clipDuration(clip) == Catch::Approx(referenceSet->clipDuration(referenceSet->findClip(clipName))));
        }
    }
    for (const Mesh& mesh : reference.meshes) {
        for (const Vertex& v : mesh.vertices) {
            for (int k = 0; k < 4; ++k) REQUIRE(v.joints[k] < skeleton.jointNames.size());
        }
    }
}

namespace {

struct RunProbe {
    AnimationSet animations;
    ExileRunRig rig;
    std::vector<glm::vec3> bind; // each joint's bind-pose position, model space

    RunProbe()
    {
        ExileKit kit;
        REQUIRE(cookExile(kExile, kit));
        REQUIRE(animations.load(kit.bundles[0]));
        REQUIRE(buildExileRunRig(animations.jointNames(0), rig));
        const std::uint32_t idle = animations.findClip(name(ExileClip::Idle));
        std::vector<glm::mat4> world;
        std::vector<glm::mat4> skin;
        glm::vec3 root;
        REQUIRE(animations.samplePose(idle, 0.0f, world));
        REQUIRE(animations.sample(idle, 0.0f, skin, root));
        for (std::size_t j = 0; j < world.size(); ++j) bind.push_back(glm::vec3(glm::inverse(skin[j]) * world[j][3]));
    }

    glm::vec3 at(const std::vector<glm::mat4>& skin, std::uint32_t joint) const { return glm::vec3(skin[joint] * glm::vec4(bind[joint], 1.0f)); }
};

float leanOf(const RunProbe& probe, const std::vector<glm::mat4>& skin)
{
    const glm::vec3 spine = probe.at(skin, probe.rig.chest) - probe.at(skin, probe.rig.hips);
    return std::atan2(-spine.z, spine.y);
}

}

TEST_CASE("running does not fold the torso over the hands: the lean stays near upright")
{
    const RunProbe probe;
    ExilePlayer& player = exile();
    ExileClipChoice run;
    run.clip = ExileClip::Run;
    run.locomotion = true;
    for (int k = 0; k < 8; ++k) {
        std::vector<glm::mat4> skin;
        REQUIRE(player.pose(run, static_cast<float>(k) / 8.0f, skin));
        CHECK(leanOf(probe, skin) < kExileRunLean + 0.02f);
        const float chestHeight = probe.at(skin, probe.rig.chest).y - probe.at(skin, probe.rig.hips).y;
        CHECK(chestHeight > 0.28f);
    }
}

TEST_CASE("the run sits the body up but leaves the arms, and so the spear and shield, at the orientation the clip gave them")
{
    const RunProbe probe;
    ExilePlayer& player = exile();
    for (const std::uint32_t leg : {2u, 3u, 4u, 6u, 7u, 8u}) {
        CHECK(std::find(probe.rig.upperBody.begin(), probe.rig.upperBody.end(), leg) == probe.rig.upperBody.end());
    }
    const std::uint32_t clip = probe.animations.findClip(name(ExileClip::Run));
    ExileClipChoice run;
    run.clip = ExileClip::Run;
    run.locomotion = true;
    for (int k = 0; k < 8; ++k) {
        const float phase = static_cast<float>(k) / 8.0f;
        std::vector<glm::mat4> skin;
        std::vector<glm::mat4> raw;
        glm::vec3 root;
        REQUIRE(player.pose(run, phase, skin));
        REQUIRE(probe.animations.sample(clip, phase * probe.animations.clipDuration(clip), raw, root));
        for (const std::uint32_t j : {probe.rig.leftArmJoints.back(), probe.rig.rightArmJoints.back()}) {
            for (int c = 0; c < 3; ++c) CHECK(glm::distance(glm::vec3(skin[j][c]), glm::vec3(raw[j][c])) < 1e-3f);
        }
        CHECK(glm::distance(probe.at(skin, probe.rig.chest), probe.at(raw, probe.rig.chest)) > 0.05f);
    }
}

TEST_CASE("blending a pose with itself gives the pose, and the idle-to-run midpoint is a valid skeleton")
{
    const RunProbe probe;
    ExilePlayer& player = exile();
    ExileClipChoice idle;
    ExileClipChoice run;
    run.clip = ExileClip::Run;
    run.locomotion = true;
    std::vector<glm::mat4> a;
    std::vector<glm::mat4> b;
    REQUIRE(player.pose(idle, 0.0f, a));
    REQUIRE(player.pose(run, 0.25f, b));
    std::vector<glm::mat4> same;
    for (const float t : {0.0f, 0.3f, 0.5f, 1.0f}) {
        blendSkin(b, b, t, same);
        for (std::size_t j = 0; j < b.size(); ++j) {
            for (int k = 0; k < 4; ++k) CHECK(glm::distance(glm::vec3(same[j][k]), glm::vec3(b[j][k])) < 1e-3f);
        }
    }
    std::vector<glm::mat4> mid;
    blendSkin(a, b, 0.5f, mid);
    const glm::vec3 hips = probe.at(mid, probe.rig.hips);
    for (std::uint32_t j = 0; j < mid.size(); ++j) {
        CHECK(glm::length(glm::vec3(mid[j][0])) == Catch::Approx(1.0f).margin(0.01f));
        CHECK(glm::distance(probe.at(mid, j), hips) < 1.2f);
    }
    CHECK(leanOf(probe, mid) < kExileRunLean + 0.1f);
}

namespace {

// The way the shield's outer face (+Y in SM_ExileRoundShield.glb) points as the player draws it, facing -Z.
glm::vec3 drawnShieldFace(const std::vector<glm::mat4>& skin)
{
    RenderView view;
    exile().appendPosed(skin, glm::dvec3(0.0), 0.0f, view);
    REQUIRE(view.instances.size() == 3);
    return glm::normalize(view.instances[2].basis * glm::vec3(0.0f, 1.0f, 0.0f));
}

float degreesBetween(glm::vec3 a, glm::vec3 b)
{
    return glm::degrees(std::acos(std::clamp(glm::dot(a, b), -1.0f, 1.0f)));
}

ExileClipChoice choiceOf(std::size_t clip, float phase)
{
    ExileClipChoice choice;
    choice.clip = static_cast<ExileClip>(clip);
    choice.locomotion = clip <= static_cast<std::size_t>(ExileClip::Run);
    choice.progress = phase;
    return choice;
}

glm::vec3 hipsFacing(const std::vector<glm::mat4>& skin)
{
    const ExileBody& body = exile().kit().body;
    return glm::normalize(glm::mat3(skin[body.hips]) * body.forward);
}

// The worst frame of a run of poses: how much faster the shield's face turns than the hips, and its largest turn.
struct ShieldTurn {
    float overHips = 0.0f;
    float largest = 0.0f;
};

void track(ShieldTurn& turn, const std::vector<glm::mat4>& before, const std::vector<glm::mat4>& after)
{
    const float face = degreesBetween(drawnShieldFace(before), drawnShieldFace(after));
    turn.overHips = std::max(turn.overHips, face - degreesBetween(hipsFacing(before), hipsFacing(after)));
    turn.largest = std::max(turn.largest, face);
}

}

// The shield faces the way the hips face, so it may turn as fast as the body does (a crossfade between swing and idle
// turns the hips 16 degrees a frame) but no faster; 25 degrees in a 60 Hz frame would be a flip whatever the body did.
TEST_CASE("the Exile's shield never snaps round between two frames, inside a clip or across a crossfade")
{
    ExilePlayer& player = exile();
    AnimationSet set;
    REQUIRE(set.load(player.kit().bundles[0]));
    std::vector<glm::mat4> a;
    std::vector<glm::mat4> b;
    std::vector<glm::mat4> last;
    std::vector<glm::mat4> mixed;
    const auto judge = [](const ShieldTurn& turn) {
        CHECK(turn.overHips < 2.0f);
        CHECK(turn.largest < 25.0f);
    };
    for (std::size_t clip = 0; clip < kExileClipCount; ++clip) {
        const float step = kDt / set.clipDuration(set.findClip(name(static_cast<ExileClip>(clip))));
        REQUIRE(player.pose(choiceOf(clip, 0.0f), 0.0f, last));
        ShieldTurn turn;
        for (float phase = step; phase <= 1.0f; phase += step) {
            REQUIRE(player.pose(choiceOf(clip, phase), phase, a));
            track(turn, last, a);
            last = a;
        }
        INFO(name(static_cast<ExileClip>(clip)) << ": the shield turns up to " << turn.largest << " deg in a frame, " << turn.overHips << " deg more than the hips");
        judge(turn);
    }
    const int fadeFrames = static_cast<int>(std::ceil(kExileFadeSeconds / kDt));
    for (std::size_t from = 0; from < kExileClipCount; ++from) {
        for (std::size_t to = 0; to < kExileClipCount; ++to) {
            for (const float phase : {0.0f, 0.5f}) {
                REQUIRE(player.pose(choiceOf(from, phase), phase, a));
                REQUIRE(player.pose(choiceOf(to, 0.0f), 0.0f, b));
                last = a;
                ShieldTurn turn;
                for (int k = 1; k <= fadeFrames; ++k) {
                    blendSkin(a, b, static_cast<float>(k) / static_cast<float>(fadeFrames), mixed);
                    track(turn, last, mixed);
                    last = mixed;
                }
                INFO(name(static_cast<ExileClip>(from)) << " at " << phase << " fading into " << name(static_cast<ExileClip>(to)) << ": up to "
                                                        << turn.largest << " deg in a frame, " << turn.overHips << " deg more than the hips");
                judge(turn);
            }
        }
    }
}
