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
