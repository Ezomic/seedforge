#include "ichor/sim/scene.h"
#include "ichor/sim/scene_io.h"
#include "ichor/stage0/combat.h"
#include "../../../engine/tests/test_temp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace ichor;
using namespace ichor::stage0;

namespace {

constexpr double kDt = 1.0 / 60.0;

struct Arena {
    Scene scene;
    Stage0 stage;
    explicit Arena(std::uint64_t seed = 1, Tuning tuning = {})
        : scene(seed)
        , stage(tuning)
    {
        stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
        scene.tick(kDt);
        stage.resetFight(scene);
        for (int i = 0; i < 30; ++i) {
            scene.tick(kDt); // settle onto the floor
        }
    }
    const Combatant& player() const { return scene.registry().get<Combatant>(scene.find(stage.playerId())); }
    const Combatant& foe() const { return scene.registry().get<Combatant>(scene.find(stage.foeId())); }
    const SkirmisherBrain& brain() const { return scene.registry().get<SkirmisherBrain>(scene.find(stage.foeId())); }
    Combatant& foeMutable() { return scene.registry().get<Combatant>(scene.find(stage.foeId())); }
    glm::dvec3 playerPosition() const { return scene.registry().get<Transform>(scene.find(stage.playerId())).position; }
};

InputFrame press(std::uint32_t buttons)
{
    InputFrame frame;
    frame.buttons = buttons;
    return frame;
}

}

TEST_CASE("an attack commits: input is refused until recovery ends and stamina is spent")
{
    Arena a;
    REQUIRE(a.player().combatState() == CombatState::Idle);
    a.scene.tick(kDt, press(InputFrame::Attack));
    REQUIRE(a.player().combatState() == CombatState::Windup);
    REQUIRE(a.player().stamina == Catch::Approx(80.0f));

    // Holding heavy during the wind-up changes nothing; the swing plays out on its frame data.
    int activeTick = -1;
    int idleTick = -1;
    for (int tick = 2; tick <= 80; ++tick) {
        a.scene.tick(kDt, press(InputFrame::Heavy));
        if (activeTick < 0 && a.player().combatState() == CombatState::Active) activeTick = tick;
        if (idleTick < 0 && a.player().combatState() == CombatState::Idle) idleTick = tick;
        if (idleTick > 0) break;
    }
    REQUIRE(activeTick == 21); // 0.35 s wind-up, counted from the tick the swing starts
    REQUIRE(idleTick == 54);   // plus 0.15 active and 0.4 recovery
    REQUIRE(a.player().stamina < 100.0f);
}

TEST_CASE("the skirmisher approaches, tells, commits and rests after three commits")
{
    Arena a(3);
    bool sawTell = false;
    bool sawRest = false;
    int commitsSeen = 0;
    BrainState previous = a.brain().brainState();
    for (int i = 0; i < 60 * 20; ++i) {
        a.scene.tick(kDt);
        const BrainState now = a.brain().brainState();
        if (now != previous) {
            if (now == BrainState::Tell) sawTell = true;
            if (now == BrainState::Committing) commitsSeen += 1;
            if (now == BrainState::Rest) sawRest = true;
            previous = now;
        }
        for (const SoundEvent& event : a.scene.soundEvents()) {
            REQUIRE(event.emitter == a.stage.foeId());
        }
        if (sawRest) break;
    }
    REQUIRE(sawTell);
    REQUIRE(commitsSeen >= 1);
    REQUIRE(sawRest);
    REQUIRE(a.stage.recorder().foe().attacks >= 1);
}

TEST_CASE("the tell cue sounds before every wind-up")
{
    Tuning tuning;
    tuning.tellCue = 42;
    tuning.lightWindupCue = 7;
    tuning.heavyWindupCue = 8;
    Arena a(4, tuning);
    int tells = 0;
    int windups = 0;
    for (int i = 0; i < 60 * 12; ++i) {
        a.scene.tick(kDt);
        for (const SoundEvent& event : a.scene.soundEvents()) {
            if (event.sound == 42) tells += 1;
            if (event.sound == 7 || event.sound == 8) windups += 1;
        }
    }
    REQUIRE(tells >= 1);
    REQUIRE(windups == tells);
}

TEST_CASE("tracking is capped during the wind-up and frozen during active frames")
{
    Arena a(5);
    // Run until the foe winds up, then walk the player sideways and watch its facing.
    for (int i = 0; i < 60 * 20 && a.foe().combatState() != CombatState::Windup; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE(a.foe().combatState() == CombatState::Windup);
    InputFrame strafe;
    strafe.moveX = 1.0f;
    float yawAtWindup = a.foe().yaw;
    while (a.foe().combatState() == CombatState::Windup) {
        a.scene.tick(kDt, strafe);
        const float turned = std::abs(a.foe().yaw - yawAtWindup);
        REQUIRE(turned <= glm::radians(30.0f) * static_cast<float>(kDt) * 1.01f + 1e-4f);
        yawAtWindup = a.foe().yaw;
    }
    REQUIRE(a.foe().combatState() == CombatState::Active);
    const float yawAtActive = a.foe().yaw;
    while (a.foe().combatState() == CombatState::Active) {
        a.scene.tick(kDt, strafe);
        REQUIRE(a.foe().yaw == yawAtActive);
    }
}

TEST_CASE("a death is classified and written as one CSV row")
{
    const auto csv = ichor::test::tempPath("ichor_fights.csv");
    std::filesystem::remove(csv);
    Arena a(6);
    a.stage.recorder().open(csv);
    for (int i = 0; i < 60 * 60 && a.player().alive(); ++i) {
        a.scene.tick(kDt); // the player stands there and takes it
    }
    REQUIRE_FALSE(a.player().alive());
    REQUIRE(a.stage.recorder().fightsWritten() == 1);
    REQUIRE(a.player().deathCause == static_cast<std::uint8_t>(DeathCause::Unreadable));

    std::string header;
    std::string row;
    std::string extra;
    bool hasExtra = false;
    {
        std::ifstream in(csv);
        REQUIRE(std::getline(in, header));
        REQUIRE(std::getline(in, row));
        hasExtra = static_cast<bool>(std::getline(in, extra));
    }
    std::filesystem::remove(csv);
    REQUIRE(header.starts_with("fight,seconds,gap_to_retry,loser,cause"));
    REQUIRE(row.starts_with("1,"));
    REQUIRE(row.find(",player,unreadable,") != std::string::npos);
    REQUIRE_FALSE(hasExtra);

    // Retry: both fighters back at spawn with full bars, and the dead one alive again.
    a.stage.resetFight(a.scene);
    a.scene.tick(kDt);
    REQUIRE(a.player().alive());
    REQUIRE(a.player().health == 100.0f);
    REQUIRE(glm::distance(a.playerPosition(), glm::dvec3(0.0, 0.0, 3.0)) < 0.2);
}

TEST_CASE("facing away from the foe does not stamp off-screen while the camera still sees it")
{
    Arena a(7);
    a.stage.setLockOn(true); // under free look the camera follows the facing, so an attacker behind the player is off screen
    for (int i = 0; i < 60 * 60 && a.player().alive(); ++i) {
        // Keep the player facing away from the foe every tick.
        auto& player = a.scene.registry().get<Combatant>(a.scene.find(a.stage.playerId()));
        player.yaw = yawTowards(a.playerPosition(), glm::dvec3(0.0, 0.0, 100.0));
        a.scene.tick(kDt);
    }
    REQUIRE_FALSE(a.player().alive());
    REQUIRE(a.player().deathCause != static_cast<std::uint8_t>(DeathCause::OffScreen));
    REQUIRE(a.stage.recorder().lastFacts().attackerOnScreen);
}

TEST_CASE("an attacker the camera cannot see is stamped off-screen")
{
    Arena a(7);
    a.stage.recorder().noteAttack(false, a.scene.time());
    auto& foe = a.scene.registry().get<Combatant>(a.scene.find(a.stage.foeId()));
    foe.attack = static_cast<std::uint8_t>(AttackKind::Light);
    a.stage.receiveHit(a.scene, a.scene.find(a.stage.playerId()), a.scene.find(a.stage.foeId()), 1000.0f, 0.0f, false);
    REQUIRE(a.player().deathCause == static_cast<std::uint8_t>(DeathCause::OffScreen));
    REQUIRE_FALSE(a.stage.recorder().lastFacts().attackerOnScreen);
}

TEST_CASE("blocking turns damage into stamina drain")
{
    Arena a(8);
    for (int i = 0; i < 60 * 60 && a.stage.recorder().foe().hits == 0; ++i) {
        a.scene.tick(kDt, press(InputFrame::Block));
    }
    REQUIRE(a.stage.recorder().foe().hits == 1);
    REQUIRE(a.stage.recorder().player().blocked == 1);
    const float damage = a.stage.recorder().player().damageTaken;
    REQUIRE((damage == Catch::Approx(12.0f) || damage == Catch::Approx(20.0f))); // 40% of 30 or 50
    REQUIRE(a.player().stamina < 100.0f);
}

TEST_CASE("a scripted fight replays to the same hash and survives a snapshot")
{
    const auto script = [](int tick) {
        InputFrame frame;
        if (tick % 90 < 20) frame.buttons = InputFrame::Attack;
        else if (tick % 90 < 35) frame.buttons = InputFrame::Block;
        else if (tick % 90 == 40) frame.buttons = InputFrame::Dodge;
        frame.moveX = (tick / 60) % 2 == 0 ? 0.5f : -0.5f;
        return frame;
    };
    Arena a(9);
    Arena b(9);
    for (int tick = 0; tick < 600; ++tick) {
        a.scene.tick(kDt, script(tick));
        b.scene.tick(kDt, script(tick));
    }
    REQUIRE(a.scene.stateHash() == b.scene.stateHash());
    REQUIRE(a.stage.recorder().player().attacks > 0);

    const auto path = ichor::test::tempPath("ichor_stage0.isav");
    REQUIRE(saveScene(path, a.scene));
    Arena c(9);
    REQUIRE(loadScene(path, c.scene));
    std::filesystem::remove(path);
    REQUIRE(c.scene.stateHash() == a.scene.stateHash());
    REQUIRE(c.foe().health == a.foe().health);
    REQUIRE(c.brain().commits == a.brain().commits);

    // The restored fight continues exactly as the live one, trades included.
    for (int tick = 600; tick < 900; ++tick) {
        a.scene.tick(kDt, script(tick));
        c.scene.tick(kDt, script(tick));
    }
    REQUIRE(c.scene.stateHash() == a.scene.stateHash());
}

TEST_CASE("retry presses are counted once each, and the setup reset is not one")
{
    Arena a(10);
    REQUIRE(a.stage.retryPresses() == 0);
    a.scene.tick(kDt, press(InputFrame::Confirm));
    a.scene.tick(kDt, press(InputFrame::Confirm));
    a.scene.tick(kDt, press(InputFrame::Confirm));
    REQUIRE(a.stage.retryPresses() == 1);
    a.scene.tick(kDt);
    a.scene.tick(kDt, press(InputFrame::Confirm));
    REQUIRE(a.stage.retryPresses() == 2);
}

TEST_CASE("retry comes from the input stream and measures hesitation")
{
    Arena a(10);
    for (int i = 0; i < 60 * 60 && a.player().alive(); ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE_FALSE(a.player().alive());
    for (int i = 0; i < 120; ++i) {
        a.scene.tick(kDt); // two seconds of staring at the corpse
    }
    a.scene.tick(kDt, press(InputFrame::Confirm));
    a.scene.tick(kDt, press(InputFrame::Confirm));
    REQUIRE(a.player().alive());
    REQUIRE(a.player().health == 100.0f);
    // Holding the button does not keep resetting.
    a.scene.tick(kDt, press(InputFrame::Confirm | InputFrame::Attack));
    a.scene.tick(kDt, press(InputFrame::Confirm));
    REQUIRE(a.player().combatState() == CombatState::Windup);

    for (int i = 0; i < 60 * 60 && a.player().alive(); ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE(a.stage.recorder().fightsWritten() == 2);
    const std::string& row = a.stage.recorder().lastRow();
    const std::size_t first = row.find(',');
    const std::size_t second = row.find(',', first + 1);
    const std::size_t third = row.find(',', second + 1);
    const double gap = std::stod(row.substr(second + 1, third - second - 1));
    REQUIRE(gap == Catch::Approx(2.0).margin(0.05));
}
