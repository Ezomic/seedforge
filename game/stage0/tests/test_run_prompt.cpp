#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/run_prompt.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ichor;
using namespace ichor::stage0;

namespace {

bool contains(const std::vector<std::string>& lines, const std::string& needle)
{
    for (const auto& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

}

TEST_CASE("a running fight shows the controls and no death text", "[run_prompt]")
{
    const auto lines = promptLines({FightOutcome::Ongoing, DeathCause::Unknown, 3});
    CHECK(contains(lines, "J light"));
    CHECK(contains(lines, "Enter retry"));
    CHECK(contains(lines, "Esc quit"));
    CHECK(contains(lines, "Fights recorded: 3"));
    CHECK_FALSE(contains(lines, "DIED"));
}

TEST_CASE("a death names its cause plainly and says how to retry", "[run_prompt]")
{
    for (int i = 0; i < static_cast<int>(kDeathCauseCount); ++i) {
        const auto cause = static_cast<DeathCause>(i);
        INFO(name(cause));
        const auto lines = promptLines({FightOutcome::PlayerDied, cause, 1});
        CHECK(contains(lines, "YOU DIED. "));
        CHECK(contains(lines, plainCause(cause)));
        CHECK(contains(lines, "Press Enter to try again."));
        CHECK_FALSE(contains(lines, "J light"));
    }
    CHECK(std::string(plainCause(DeathCause::OutOfStamina)) != plainCause(DeathCause::Cornered));
}

TEST_CASE("an out-of-range cause falls back to the unknown text", "[run_prompt]")
{
    CHECK(std::string(plainCause(static_cast<DeathCause>(200))) == plainCause(DeathCause::Unknown));
}

TEST_CASE("a win offers the next fight", "[run_prompt]")
{
    const auto lines = promptLines({FightOutcome::FoeDied, DeathCause::Unknown, 2});
    CHECK(contains(lines, "YOU WON."));
    CHECK(contains(lines, "Press Enter for the next fight."));
}

TEST_CASE("the live overlay shows the controls on a fresh fight", "[run_prompt]")
{
    Scene scene(1);
    Stage0 stage;
    stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    scene.tick(1.0 / 60.0);
    stage.resetFight(scene);
    std::vector<std::string> lines;
    stage.overlay(scene, lines);
    CHECK(contains(lines, "Enter retry"));
    CHECK_FALSE(contains(lines, "YOU DIED"));
}

TEST_CASE("the live overlay shows the death message once the player is dead", "[run_prompt]")
{
    Scene scene(1);
    Stage0 stage;
    stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    scene.tick(1.0 / 60.0);
    stage.resetFight(scene);
    auto& combatant = scene.registry().get<Combatant>(scene.find(stage.playerId()));
    combatant.health = 0.0f;
    combatant.state = static_cast<std::uint8_t>(CombatState::Dead);
    combatant.deathCause = static_cast<std::uint8_t>(DeathCause::Cornered);
    std::vector<std::string> lines;
    stage.overlay(scene, lines);
    CHECK(contains(lines, plainCause(DeathCause::Cornered)));
    CHECK(contains(lines, "Press Enter to try again."));
}

namespace {

constexpr double kTick = 1.0 / 60.0;

InputFrame buttons(std::uint32_t pressed)
{
    InputFrame frame;
    frame.buttons = pressed;
    return frame;
}

bool confirms(const InputFrame& frame) { return (frame.buttons & InputFrame::Confirm) != 0; }

void kill(Scene& scene, const Stage0& stage, DeathCause cause)
{
    auto& combatant = scene.registry().get<Combatant>(scene.find(stage.playerId()));
    combatant.health = 0.0f;
    combatant.state = static_cast<std::uint8_t>(CombatState::Dead);
    combatant.deathCause = static_cast<std::uint8_t>(cause);
}

}

TEST_CASE("every recorded cause has a line of its own, and an unknown or missing one a generic line", "[run_prompt]")
{
    const std::string generic = plainCause(DeathCause::Unknown);
    for (int i = 1; i < static_cast<int>(kDeathCauseCount); ++i) {
        const auto cause = static_cast<DeathCause>(i);
        INFO(name(cause));
        CHECK(std::string(plainCause(cause)) != generic);
        const auto card = deathCard(cause);
        REQUIRE(card.size() == 3);
        CHECK(card[0] == "YOU DIED");
        CHECK(card[1] == plainCause(cause));
        CHECK(card[2] == retryHint());
    }
    CHECK(deathCard(DeathCause::Unknown)[1] == generic);
    CHECK(deathCard(static_cast<DeathCause>(200))[1] == generic);
}

TEST_CASE("the stage reports the player's death and its cause, and nothing while they live", "[run_prompt]")
{
    Scene scene(1);
    Stage0 stage;
    stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    scene.tick(kTick);
    stage.resetFight(scene);
    CHECK_FALSE(stage.playerDeath(scene).has_value());
    kill(scene, stage, DeathCause::OutOfStamina);
    REQUIRE(stage.playerDeath(scene).has_value());
    CHECK(*stage.playerDeath(scene) == DeathCause::OutOfStamina);
    kill(scene, stage, DeathCause::Unknown);
    CHECK(*stage.playerDeath(scene) == DeathCause::Unknown);
}

TEST_CASE("the retry guard passes everything while the player lives", "[run_prompt]")
{
    RetryGuard guard;
    CHECK(confirms(guard.filter(buttons(InputFrame::Confirm), false, 0.0)));
    CHECK(confirms(guard.filter(buttons(InputFrame::Confirm), false, 5.0)));
}

TEST_CASE("the retry guard holds back Enter until the death has shown a moment and the key has been let go", "[run_prompt]")
{
    RetryGuard guard;
    // Pressed on the very frame the death shows.
    CHECK_FALSE(confirms(guard.filter(buttons(InputFrame::Confirm), true, 10.0)));
    // Still held well past the window: never a retry without a fresh press.
    CHECK_FALSE(confirms(guard.filter(buttons(InputFrame::Confirm), true, 10.0 + RetryGuard::kArmSeconds + 1.0)));
    CHECK_FALSE(confirms(guard.filter(buttons(0), true, 10.0 + RetryGuard::kArmSeconds + 1.1)));
    CHECK(confirms(guard.filter(buttons(InputFrame::Confirm), true, 10.0 + RetryGuard::kArmSeconds + 1.2)));
}

TEST_CASE("the retry guard ignores a release and press inside the window", "[run_prompt]")
{
    RetryGuard guard;
    const double window = RetryGuard::kArmSeconds;
    CHECK_FALSE(confirms(guard.filter(buttons(0), true, 0.0)));
    CHECK_FALSE(confirms(guard.filter(buttons(InputFrame::Confirm), true, window * 0.3)));
    CHECK_FALSE(confirms(guard.filter(buttons(0), true, window * 0.6)));
    CHECK_FALSE(confirms(guard.filter(buttons(InputFrame::Confirm), true, window * 0.9)));
    // Held across the end of the window, so the sim would see a fresh edge the moment it opened.
    CHECK_FALSE(confirms(guard.filter(buttons(InputFrame::Confirm), true, window * 1.5)));
    CHECK_FALSE(confirms(guard.filter(buttons(0), true, window * 1.6)));
    CHECK(confirms(guard.filter(buttons(InputFrame::Confirm), true, window * 1.7)));
}

TEST_CASE("the retry guard leaves the other buttons alone and arms again on the next death", "[run_prompt]")
{
    RetryGuard guard;
    const InputFrame held = guard.filter(buttons(InputFrame::Confirm | InputFrame::Attack), true, 0.0);
    CHECK_FALSE(confirms(held));
    CHECK((held.buttons & InputFrame::Attack) != 0);
    CHECK_FALSE(confirms(guard.filter(buttons(0), true, 1.0)));
    CHECK(confirms(guard.filter(buttons(InputFrame::Confirm), true, 1.1)));
    CHECK(confirms(guard.filter(buttons(InputFrame::Confirm), false, 1.2)));
    CHECK_FALSE(confirms(guard.filter(buttons(InputFrame::Confirm), true, 30.0)));
}

TEST_CASE("Enter pressed on the frame the death shows does not restart the fight; a press after the window does", "[run_prompt]")
{
    Scene scene(1);
    Stage0 stage;
    stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    scene.tick(kTick);
    stage.resetFight(scene);
    scene.tick(kTick);
    kill(scene, stage, DeathCause::Cornered);
    RetryGuard guard;
    const auto feed = [&](std::uint32_t pressed) {
        scene.tick(kTick, guard.filter(buttons(pressed), stage.playerDeath(scene).has_value(), scene.time()));
    };

    // Without the guard this press restarts the fight on the next tick.
    feed(InputFrame::Confirm);
    CHECK(stage.playerDeath(scene).has_value());
    for (int i = 0; i < static_cast<int>(RetryGuard::kArmSeconds / kTick) + 2; ++i) feed(InputFrame::Confirm);
    CHECK(stage.playerDeath(scene).has_value());
    feed(0);
    CHECK(stage.playerDeath(scene).has_value());
    feed(InputFrame::Confirm);
    CHECK_FALSE(stage.playerDeath(scene).has_value());
}

TEST_CASE("without the guard the same press restarts the fight at once, which is what the guard prevents", "[run_prompt]")
{
    Scene scene(1);
    Stage0 stage;
    stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    scene.tick(kTick);
    stage.resetFight(scene);
    scene.tick(kTick);
    kill(scene, stage, DeathCause::Cornered);
    scene.tick(kTick, buttons(InputFrame::Confirm));
    CHECK_FALSE(stage.playerDeath(scene).has_value());
}
