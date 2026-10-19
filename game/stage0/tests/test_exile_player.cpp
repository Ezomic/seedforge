#include "ichor/gfx/null_renderer.h"
#include "ichor/sim/scene.h"
#include "ichor/stage0/exile_player.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <cmath>
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
    CHECK(chooseExileClip(guard, tuning, 0.0f, kDt).progress == 1.0f);
    guard.blockTicks = -5;
    CHECK(chooseExileClip(guard, tuning, 0.0f, kDt).progress == 0.0f);
}

TEST_CASE("the playback rate keeps the feet on the ground within a range")
{
    CHECK(exileLocomotionRate(5.2f, 5.2f) == Catch::Approx(1.0f));
    CHECK(exileLocomotionRate(2.6f, 5.2f) == Catch::Approx(0.5f));
    CHECK(exileLocomotionRate(0.0f, 5.2f) == Catch::Approx(0.4f));
    CHECK(exileLocomotionRate(99.0f, 5.2f) == Catch::Approx(1.6f));
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
