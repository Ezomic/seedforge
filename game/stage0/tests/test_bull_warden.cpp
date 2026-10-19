#include "ichor/stage0/bull_warden.h"
#include "ichor/stage0/character_visuals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

using namespace ichor;
using namespace ichor::stage0;

namespace {

const std::filesystem::path kBullFolder = std::filesystem::path(ICHOR_ART_DIR) / "monsters" / "bull_warden";

const BullWardenKit& kit()
{
    static const std::unique_ptr<BullWardenKit> instance = [] {
        auto k = std::make_unique<BullWardenKit>();
        REQUIRE(cookBullWarden(kBullFolder, *k));
        return k;
    }();
    return *instance;
}

std::uint32_t jointIndex(std::string_view name)
{
    const auto& names = kit().bundle.skeletons[0].jointNames;
    const auto it = std::find(names.begin(), names.end(), name);
    REQUIRE(it != names.end());
    return static_cast<std::uint32_t>(it - names.begin());
}

glm::vec3 jointPosition(const BullWardenPose& pose, std::string_view name)
{
    std::vector<glm::mat4> model;
    REQUIRE(kit().animations.samplePose(pose.clip, pose.time, model));
    return glm::vec3(model[jointIndex(name)][3]);
}

Combatant inState(CombatState state, float time, AttackKind attack = AttackKind::Light)
{
    Combatant c;
    c.state = static_cast<std::uint8_t>(state);
    c.attack = static_cast<std::uint8_t>(attack);
    c.stateTime = time;
    return c;
}

BullWardenPose poseOf(const Combatant& c, bool moving = false)
{
    return bullWardenPose(c, Tuning{}, kit().clips, kit().swing, moving, 1.0);
}

// Lowest point of the skinned mesh, lifted as the renderer lifts it.
float lowestVertex(const BullWardenPose& pose)
{
    BullWardenFrame frame;
    REQUIRE(bullWardenFrame(kit(), pose, frame));
    float lowest = std::numeric_limits<float>::max();
    for (const Vertex& v : kit().bundle.meshes[0].vertices) {
        glm::vec4 p(0.0f);
        for (int k = 0; k < 4; ++k) p += (static_cast<float>(v.weights[k]) / 65535.0f) * (frame.skin[v.joints[k]] * glm::vec4(v.position, 1.0f));
        lowest = std::min(lowest, p.y);
    }
    return lowest + frame.lift;
}

float axeSpeed(const BullWardenPose& a, const BullWardenPose& b)
{
    return glm::length(jointPosition(a, "mixamorig:Axe") - jointPosition(b, "mixamorig:Axe")) / std::max(b.time - a.time, 1e-3f);
}

struct Arena {
    Scene scene{1};
    Stage0 stage;
    Arena()
    {
        stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
        scene.tick(1.0 / 60.0);
        stage.resetFight(scene);
    }
};

}

TEST_CASE("the Bull-Warden cooks with its five clips and 27 joints")
{
    REQUIRE(kit().clips.complete());
    CHECK(kit().bundle.skeletons[0].jointNames.size() == 27);
    CHECK(kit().clips.attackDuration > 2.0f);
}

TEST_CASE("the cooked Bull-Warden faces -Z like Stage 0 characters")
{
    const BullWardenPose idle{kit().clips.idle, 0.0f};
    const glm::vec3 forward = jointPosition(idle, "mixamorig:LeftToeBase") - jointPosition(idle, "mixamorig:LeftFoot");
    CHECK(forward.z < -0.1f);
}

TEST_CASE("the lift puts the model on the floor in every state, where the raw clips sink it")
{
    for (const Combatant& c : {inState(CombatState::Idle, 0.0f), inState(CombatState::Windup, 0.3f), inState(CombatState::Active, 0.1f),
             inState(CombatState::Recovery, 0.2f), inState(CombatState::Staggered, 0.4f)}) {
        const float lowest = lowestVertex(poseOf(c));
        // The authored chop drives the axe head about a metre into the floor, which is the clip and not the lift.
        const float floor = c.combatState() == CombatState::Active ? -1.2f : -0.6f;
        CHECK(lowest > floor);
        CHECK(lowest < 0.3f);
    }
    BullWardenFrame frame;
    REQUIRE(bullWardenFrame(kit(), poseOf(inState(CombatState::Idle, 0.0f)), frame));
    CHECK(frame.lift > 0.5f);
}

TEST_CASE("each combat state picks its clip")
{
    CHECK(poseOf(inState(CombatState::Idle, 0.0f)).clip == kit().clips.idle);
    CHECK(poseOf(inState(CombatState::Idle, 0.0f), true).clip == kit().clips.walk);
    CHECK(poseOf(inState(CombatState::Windup, 0.1f)).clip == kit().clips.attack);
    CHECK(poseOf(inState(CombatState::Active, 0.1f)).clip == kit().clips.attack);
    CHECK(poseOf(inState(CombatState::Recovery, 0.1f)).clip == kit().clips.attack);
    CHECK(poseOf(inState(CombatState::Staggered, 0.1f)).clip == kit().clips.stagger);
    CHECK(poseOf(inState(CombatState::Dead, 0.1f)).clip == kit().clips.death);
}

TEST_CASE("the attack clip is stretched over wind-up, active and recovery so the swing lands in the active window")
{
    const Tuning t;
    const float d = kit().clips.attackDuration;
    CHECK(poseOf(inState(CombatState::Windup, 0.0f)).time == Catch::Approx(0.0f));
    CHECK(poseOf(inState(CombatState::Windup, t.lightWindup)).time == Catch::Approx(kit().swing.strikeStart * d));
    CHECK(poseOf(inState(CombatState::Active, 0.0f)).time == Catch::Approx(kit().swing.strikeStart * d));
    CHECK(poseOf(inState(CombatState::Active, t.lightActive)).time == Catch::Approx(kit().swing.strikeEnd * d));
    CHECK(poseOf(inState(CombatState::Recovery, t.lightRecovery)).time == Catch::Approx(d));
    CHECK(poseOf(inState(CombatState::Windup, t.heavyWindup, AttackKind::Heavy)).time == Catch::Approx(kit().swing.strikeStart * d));

    // Mean axe speed in clip time: the swing must be the fast part of the clip, or the
    // telegraph would show a strike that the clip does not make.
    const auto meanSpeed = [&](CombatState state, float length) {
        float total = 0.0f;
        constexpr int kSteps = 12;
        for (int i = 0; i < kSteps; ++i) {
            total += axeSpeed(poseOf(inState(state, length * static_cast<float>(i) / kSteps)), poseOf(inState(state, length * static_cast<float>(i + 1) / kSteps)));
        }
        return total / kSteps;
    };
    const float active = meanSpeed(CombatState::Active, t.lightActive);
    CHECK(active > 2.0f * meanSpeed(CombatState::Windup, t.lightWindup));
    CHECK(active > 2.0f * meanSpeed(CombatState::Recovery, t.lightRecovery));
}

TEST_CASE("the wind-up lifts the axe above where it rests")
{
    const float rest = jointPosition(poseOf(inState(CombatState::Idle, 0.0f)), "mixamorig:Axe").y;
    const float raised = jointPosition(poseOf(inState(CombatState::Windup, Tuning{}.lightWindup)), "mixamorig:Axe").y;
    CHECK(raised > rest + 0.5f);
}

TEST_CASE("hostile combat times give finite poses inside the clip")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float time : {nan, inf, -inf, -5.0f, 1e30f}) {
        for (const CombatState state : {CombatState::Idle, CombatState::Windup, CombatState::Active, CombatState::Recovery,
                 CombatState::Dodging, CombatState::Staggered, CombatState::Dead}) {
            const BullWardenPose p = poseOf(inState(state, time));
            REQUIRE(std::isfinite(p.time));
            CHECK(p.time >= 0.0f);
            CHECK(p.time <= kit().animations.clipDuration(p.clip) + 1e-3f);
        }
    }
    Tuning broken;
    broken.lightWindup = 0.0f;
    broken.staggerDuration = -1.0f;
    CHECK(std::isfinite(bullWardenPose(inState(CombatState::Windup, 1.0f), broken, kit().clips, kit().swing, false, nan).time));
    CHECK(std::isfinite(bullWardenPose(inState(CombatState::Staggered, 1.0f), broken, kit().clips, kit().swing, false, inf).time));
}

TEST_CASE("only the foe is replaced: its grey body is skipped and the Bull-Warden is added with a joint palette")
{
    Arena arena;
    const GreyBoxMeshes grey{0, 1, 2};
    RenderView plain;
    appendCharacterInstances(arena.scene, 1.0, Tuning{}, grey, plain.instances);
    RenderView swapped;
    appendCharacterInstances(arena.scene, 1.0, Tuning{}, grey, swapped.instances, arena.scene.find(arena.stage.foeId()));
    CHECK(plain.instances.size() == 6);
    CHECK(swapped.instances.size() == 3);

    BullWardenKit own;
    REQUIRE(cookBullWarden(kBullFolder, own));
    own.mesh = 7;
    appendBullWarden(arena.scene, arena.stage.foeId(), 1.0, Tuning{}, own, swapped);
    REQUIRE(swapped.instances.size() == 4);
    CHECK(swapped.instances.back().mesh == 7);
    CHECK(swapped.instances.back().jointCount == 27);
    CHECK(swapped.jointPalette.size() == 27);
    const glm::dvec3 foeFeet = arena.scene.registry().get<Transform>(arena.scene.find(arena.stage.foeId())).position;
    CHECK(swapped.instances.back().position.x == Catch::Approx(foeFeet.x));
    CHECK(swapped.instances.back().position.z == Catch::Approx(foeFeet.z));
}

TEST_CASE("drawing the Bull-Warden does not move the simulation")
{
    Arena arena;
    const std::uint64_t before = arena.scene.stateHash();
    BullWardenKit own;
    REQUIRE(cookBullWarden(kBullFolder, own));
    RenderView view;
    appendBullWarden(arena.scene, arena.stage.foeId(), 0.5, Tuning{}, own, view);
    CHECK(arena.scene.stateHash() == before);
}
