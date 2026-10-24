#include "ichor/combat_bridge/combat_presentation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <cmath>
#include <limits>

using namespace ichor;

namespace {

stage0::Combatant enemyIn(stage0::CombatState state, float stateTime, stage0::AttackKind attack = stage0::AttackKind::Light)
{
    stage0::Combatant enemy;
    enemy.side = 1;
    enemy.state = static_cast<std::uint8_t>(state);
    enemy.attack = static_cast<std::uint8_t>(attack);
    enemy.stateTime = stateTime;
    return enemy;
}

double totalLength(const std::vector<LineSegment>& lines)
{
    double sum = 0.0;
    for (const LineSegment& line : lines) sum += glm::length(line.b - line.a);
    return sum;
}

std::vector<LineSegment> cone(const EnemyTelegraph& telegraph, const std::function<double(double, double)>& ground = {})
{
    std::vector<LineSegment> lines;
    appendEnemyTelegraph(telegraph, {10.0, 2.0, -5.0}, {0.0f, 0.0f, -1.0f}, stage0::Tuning{}, ground, lines);
    return lines;
}

}

TEST_CASE("the telegraph phase follows the combat state and the tuning's timings", "[combat_presentation]")
{
    const stage0::Tuning tuning;

    SECTION("a light wind-up charges over the light timing")
    {
        const EnemyTelegraph start = enemyTelegraph(enemyIn(stage0::CombatState::Windup, 0.0f), tuning);
        const EnemyTelegraph half = enemyTelegraph(enemyIn(stage0::CombatState::Windup, tuning.lightWindup * 0.5f), tuning);
        const EnemyTelegraph done = enemyTelegraph(enemyIn(stage0::CombatState::Windup, tuning.lightWindup * 3.0f), tuning);
        CHECK(start.phase == TelegraphPhase::Windup);
        CHECK(start.progress == 0.0f);
        CHECK(half.progress == Catch::Approx(0.5f));
        CHECK(done.progress == 1.0f);
    }
    SECTION("a heavy wind-up takes the heavy timing")
    {
        const EnemyTelegraph half = enemyTelegraph(enemyIn(stage0::CombatState::Windup, tuning.lightWindup, stage0::AttackKind::Heavy), tuning);
        CHECK(half.progress == Catch::Approx(tuning.lightWindup / tuning.heavyWindup));
    }
    SECTION("the live blow is full and the recovery drains")
    {
        CHECK(enemyTelegraph(enemyIn(stage0::CombatState::Active, 0.05f), tuning).phase == TelegraphPhase::Active);
        CHECK(enemyTelegraph(enemyIn(stage0::CombatState::Active, 0.05f), tuning).progress == 1.0f);
        const EnemyTelegraph early = enemyTelegraph(enemyIn(stage0::CombatState::Recovery, 0.0f), tuning);
        const EnemyTelegraph late = enemyTelegraph(enemyIn(stage0::CombatState::Recovery, tuning.lightRecovery * 0.75f), tuning);
        CHECK(early.phase == TelegraphPhase::Recovery);
        CHECK(early.progress == 1.0f);
        CHECK(late.progress == Catch::Approx(0.25f));
    }
    SECTION("states that are not an attack show nothing")
    {
        for (const auto state : {stage0::CombatState::Idle, stage0::CombatState::Dodging, stage0::CombatState::Staggered, stage0::CombatState::Dead}) {
            CHECK(enemyTelegraph(enemyIn(state, 0.2f), tuning).phase == TelegraphPhase::None);
        }
    }
    SECTION("a nonsense timer or timing stays in range")
    {
        stage0::Tuning odd;
        odd.lightWindup = 0.0f;
        CHECK(enemyTelegraph(enemyIn(stage0::CombatState::Windup, 0.1f), odd).progress == 1.0f);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const EnemyTelegraph broken = enemyTelegraph(enemyIn(stage0::CombatState::Windup, nan), tuning);
        CHECK(broken.progress >= 0.0f);
        CHECK(broken.progress <= 1.0f);
    }
}

TEST_CASE("the debug aids are off unless F3 is at full or --combat-debug asks", "[combat_presentation]")
{
    for (const OverlayMode mode : {OverlayMode::Off, OverlayMode::Compact}) {
        CHECK_FALSE(combatDebugAids(mode, false).stageLines);
        CHECK_FALSE(combatDebugAids(mode, false).colliders);
        CHECK(combatDebugAids(mode, true).stageLines);
        CHECK(combatDebugAids(mode, true).colliders);
    }
    CHECK(combatDebugAids(OverlayMode::Full, false).stageLines);
    CHECK(combatDebugAids(OverlayMode::Full, false).colliders);
}

TEST_CASE("the ground cone grows through the wind-up, is solid while live and drains in the recovery", "[combat_presentation]")
{
    const double early = totalLength(cone({TelegraphPhase::Windup, 0.1f}));
    const double late = totalLength(cone({TelegraphPhase::Windup, 1.0f}));
    const double live = totalLength(cone({TelegraphPhase::Active, 1.0f}));
    const double spent = totalLength(cone({TelegraphPhase::Recovery, 0.1f}));
    CHECK(late > early * 2.0);
    CHECK(live == Catch::Approx(late));
    CHECK(spent < late * 0.5);
    CHECK(cone({TelegraphPhase::Windup, 1.0f}).size() > 40);
}

TEST_CASE("the ground cone shows nothing for no attack, and rejects hostile input", "[combat_presentation]")
{
    CHECK(cone({}).empty());
    std::vector<LineSegment> lines;
    const stage0::Tuning tuning;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    appendEnemyTelegraph({TelegraphPhase::Windup, 1.0f}, {0.0, 0.0, 0.0}, {0.0f, 0.0f, 0.0f}, tuning, {}, lines);
    appendEnemyTelegraph({TelegraphPhase::Windup, 1.0f}, {0.0, 0.0, 0.0}, {nan, 0.0f, 1.0f}, tuning, {}, lines);
    appendEnemyTelegraph({TelegraphPhase::Windup, 1.0f}, {nan, 0.0, 0.0}, {0.0f, 0.0f, 1.0f}, tuning, {}, lines);
    CHECK(lines.empty());
    appendEnemyTelegraph({TelegraphPhase::Windup, nan}, {0.0, 0.0, 0.0}, {0.0f, 0.0f, 1.0f}, tuning, {}, lines);
    for (const LineSegment& line : lines) {
        CHECK(std::isfinite(line.a.x + line.a.y + line.a.z + line.b.x + line.b.y + line.b.z));
    }
}

TEST_CASE("a broken tuning draws no cone and cannot run away", "[combat_presentation]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float bad : {nan, inf, -inf, 1.0e9f, -1.0f}) {
        for (int field = 0; field < 3; ++field) {
            stage0::Tuning tuning;
            (field == 0 ? tuning.attackRange : field == 1 ? tuning.attackRadius : tuning.bodyRadius) = bad;
            std::vector<LineSegment> lines;
            appendEnemyTelegraph({TelegraphPhase::Active, 1.0f}, {0.0, 0.0, 0.0}, {0.0f, 0.0f, 1.0f}, tuning, {}, lines);
            CHECK(lines.empty());
        }
    }
}

TEST_CASE("the ground cone lies on a sloped ground and starts at the enemy's feet", "[combat_presentation]")
{
    const auto slope = [](double x, double) { return 0.5 * x; };
    const std::vector<LineSegment> lines = cone({TelegraphPhase::Active, 1.0f}, slope);
    REQUIRE_FALSE(lines.empty());
    double nearest = 1e9;
    for (const LineSegment& line : lines) {
        for (const glm::dvec3 p : {line.a, line.b}) {
            CHECK(p.y == Catch::Approx(slope(p.x, p.z) + 0.06));
            nearest = std::min(nearest, std::hypot(p.x - 10.0, p.z + 5.0));
        }
    }
    CHECK(nearest < 0.5);

    const std::vector<LineSegment> flat = cone({TelegraphPhase::Active, 1.0f});
    for (const LineSegment& line : flat) CHECK(line.a.y == Catch::Approx(2.06));
}
