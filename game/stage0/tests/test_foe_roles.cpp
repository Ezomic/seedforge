#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;
using namespace ichor::stage0;

namespace {

constexpr double kDt = 1.0 / 60.0;

struct Arena {
    Scene scene;
    Stage0 stage;
    Arena(FoeRole role, glm::dvec3 player, glm::dvec3 foe, std::uint64_t seed = 1)
        : scene(seed)
    {
        stage.setFoeRole(role);
        stage.install(scene, player, foe);
        scene.tick(kDt);
        stage.resetFight(scene);
        for (int i = 0; i < 30; ++i) {
            scene.tick(kDt);
        }
        stage.resetFight(scene); // the bodies have landed; the brains start again from nothing
    }
    entt::entity playerEntity() { return scene.find(stage.playerId()); }
    entt::entity foeEntity() { return scene.find(stage.foeId()); }
    Combatant& player() { return scene.registry().get<Combatant>(playerEntity()); }
    Combatant& foe() { return scene.registry().get<Combatant>(foeEntity()); }
    Transform& playerTransform() { return scene.registry().get<Transform>(playerEntity()); }
    Transform& foeTransform() { return scene.registry().get<Transform>(foeEntity()); }
    double distance() { return glm::length(glm::dvec3(playerTransform().position - foeTransform().position)); }
    void tick(int count)
    {
        for (int i = 0; i < count; ++i) scene.tick(kDt);
    }
};

struct AnchorArena : Arena {
    AnchorArena(double gap, std::uint64_t seed = 1)
        : Arena(FoeRole::Anchor, {0.0, 0.0, 8.0}, {0.0, 0.0, 8.0 - gap}, seed)
    {
    }
    AnchorBrain& brain() { return scene.registry().get<AnchorBrain>(foeEntity()); }
    bool tickUntil(AnchorState state, int limit)
    {
        for (int i = 0; i < limit; ++i) {
            scene.tick(kDt);
            if (brain().anchorState() == state) return true;
        }
        return false;
    }
};

struct ArtilleryArena : Arena {
    ArtilleryArena(double gap, std::uint64_t seed = 1)
        : Arena(FoeRole::Artillery, {0.0, 0.0, 8.0}, {0.0, 0.0, 8.0 - gap}, seed)
    {
    }
    ArtilleryBrain& brain() { return scene.registry().get<ArtilleryBrain>(foeEntity()); }
    bool tickUntil(ArtilleryState state, int limit)
    {
        for (int i = 0; i < limit; ++i) {
            scene.tick(kDt);
            if (brain().artilleryState() == state) return true;
        }
        return false;
    }
};

}

TEST_CASE("the anchor role carries its own brain, health and poise")
{
    AnchorArena a(6.0);
    REQUIRE(a.scene.registry().all_of<AnchorBrain>(a.foeEntity()));
    REQUIRE_FALSE(a.scene.registry().all_of<SkirmisherBrain>(a.foeEntity()));
    REQUIRE_FALSE(a.scene.registry().all_of<ArtilleryBrain>(a.foeEntity()));
    CHECK(a.foe().health == Catch::Approx(a.stage.anchorTuning().maxHealth));
    CHECK(a.foe().poise == Catch::Approx(a.stage.anchorTuning().maxPoise));
}

TEST_CASE("the artillery role carries its own brain, health and poise")
{
    ArtilleryArena a(12.0);
    REQUIRE(a.scene.registry().all_of<ArtilleryBrain>(a.foeEntity()));
    REQUIRE_FALSE(a.scene.registry().all_of<SkirmisherBrain>(a.foeEntity()));
    REQUIRE_FALSE(a.scene.registry().all_of<AnchorBrain>(a.foeEntity()));
    CHECK(a.foe().health == Catch::Approx(a.stage.artilleryTuning().maxHealth));
    CHECK(a.foe().poise == Catch::Approx(a.stage.artilleryTuning().maxPoise));
}

TEST_CASE("changing the role on a reset swaps the brain")
{
    Arena a(FoeRole::Anchor, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    a.stage.setFoeRole(FoeRole::Artillery);
    a.stage.resetFight(a.scene);
    CHECK_FALSE(a.scene.registry().all_of<AnchorBrain>(a.foeEntity()));
    CHECK(a.scene.registry().all_of<ArtilleryBrain>(a.foeEntity()));
    a.stage.setFoeRole(FoeRole::Skirmisher);
    a.stage.resetFight(a.scene);
    CHECK_FALSE(a.scene.registry().all_of<ArtilleryBrain>(a.foeEntity()));
    CHECK(a.scene.registry().all_of<SkirmisherBrain>(a.foeEntity()));
    CHECK(a.foe().health == Catch::Approx(a.stage.tuning().maxHealth));
}

TEST_CASE("a slam takes exactly its wind-up in ticks, then hits a player inside the radius")
{
    AnchorArena a(2.0);
    const AnchorTuning& t = a.stage.anchorTuning();
    REQUIRE(a.tickUntil(AnchorState::Windup, 120));
    CHECK(a.player().health == Catch::Approx(a.stage.tuning().maxHealth));
    int ticks = 0;
    while (a.brain().anchorState() == AnchorState::Windup && ticks < 200) {
        a.tick(1);
        ++ticks;
    }
    CHECK(ticks == t.windupTicks);
    CHECK(a.brain().slams == 1);
    CHECK(a.player().health == Catch::Approx(a.stage.tuning().maxHealth - t.slamDamage));
    CHECK(a.player().poise < a.stage.tuning().maxPoise);
}

TEST_CASE("a player outside the slam radius is not hit")
{
    AnchorArena a(3.2);
    REQUIRE(a.tickUntil(AnchorState::Windup, 120));
    a.tick(a.stage.anchorTuning().windupTicks + 5);
    CHECK(a.brain().slams == 1);
    CHECK(a.player().health == Catch::Approx(a.stage.tuning().maxHealth));
}

TEST_CASE("an anchor with a broken poise loses its slam")
{
    AnchorArena a(2.0);
    REQUIRE(a.tickUntil(AnchorState::Windup, 120));
    a.tick(10);
    a.stage.receiveHit(a.scene, a.foeEntity(), a.playerEntity(), 0.0f, a.stage.anchorTuning().maxPoise, true);
    REQUIRE(a.foe().combatState() == CombatState::Staggered);
    a.tick(1);
    CHECK(a.brain().anchorState() == AnchorState::Staggered);
    a.tick(static_cast<int>(a.stage.tuning().staggerDuration * 60.0) - 2);
    CHECK(a.brain().slams == 0);
    CHECK(a.player().health == Catch::Approx(a.stage.tuning().maxHealth));
}

TEST_CASE("the anchor's poise comes back to its own cap, not the standard one")
{
    AnchorArena a(6.0);
    a.stage.receiveHit(a.scene, a.foeEntity(), a.playerEntity(), 0.0f, a.stage.anchorTuning().maxPoise, true);
    REQUIRE(a.foe().combatState() == CombatState::Staggered);
    a.tick(static_cast<int>(a.stage.tuning().staggerDuration * 60.0) + 5);
    CHECK(a.foe().combatState() != CombatState::Staggered);
    CHECK(a.foe().poise == Catch::Approx(a.stage.anchorTuning().maxPoise));
    CHECK(a.foe().poise > a.stage.tuning().maxPoise);
}

TEST_CASE("a standard blow does not stagger an anchor, a standard fighter's poise would have broken")
{
    AnchorArena a(6.0);
    a.stage.receiveHit(a.scene, a.foeEntity(), a.playerEntity(), 0.0f, a.stage.tuning().heavyPoiseDamage, true);
    a.stage.receiveHit(a.scene, a.foeEntity(), a.playerEntity(), 0.0f, a.stage.tuning().heavyPoiseDamage, true);
    CHECK(a.foe().combatState() != CombatState::Staggered);
    CHECK(a.foe().poise == Catch::Approx(a.stage.anchorTuning().maxPoise - 2.0f * a.stage.tuning().heavyPoiseDamage));
}

TEST_CASE("the anchor walks up to slam range and plants")
{
    AnchorArena a(12.0);
    a.tick(60 * 12);
    CHECK(a.brain().slams >= 1);
    CHECK(a.distance() <= a.stage.anchorTuning().slamRange + 0.2);
}

TEST_CASE("a dead anchor stops moving and slamming")
{
    AnchorArena a(6.0);
    a.stage.receiveHit(a.scene, a.foeEntity(), a.playerEntity(), 1000.0f, 0.0f, true);
    REQUIRE_FALSE(a.foe().alive());
    a.tick(60 * 4);
    CHECK(a.brain().slams == 0);
    CHECK(a.player().health == Catch::Approx(a.stage.tuning().maxHealth));
}

TEST_CASE("the artillery marks the player's spot and the blast lands there after its telegraph")
{
    ArtilleryArena a(12.0);
    const ArtilleryTuning& t = a.stage.artilleryTuning();
    REQUIRE(a.tickUntil(ArtilleryState::Aim, 60 * 10));
    const double markX = a.brain().aimX;
    const double markZ = a.brain().aimY;
    CHECK(markX == Catch::Approx(a.playerTransform().position.x).margin(0.01));
    CHECK(markZ == Catch::Approx(a.playerTransform().position.z).margin(0.01));
    int ticks = 0;
    while (a.brain().artilleryState() == ArtilleryState::Aim && ticks < 200) {
        a.tick(1);
        ++ticks;
    }
    CHECK(ticks == t.windupTicks);
    CHECK(a.brain().shots == 1);
    CHECK(a.player().health == Catch::Approx(a.stage.tuning().maxHealth - t.blastDamage));
}

TEST_CASE("a player who leaves the marked point is not hit")
{
    ArtilleryArena a(12.0);
    REQUIRE(a.tickUntil(ArtilleryState::Aim, 60 * 10));
    a.scene.enqueue(MoveCommand{a.stage.playerId(), glm::dvec3(a.stage.artilleryTuning().blastRadius + 2.0, 0.0, 0.0)});
    int ticks = 0;
    while (a.brain().artilleryState() == ArtilleryState::Aim && ticks < 200) {
        a.tick(1);
        ++ticks;
    }
    CHECK(a.brain().shots == 1);
    CHECK(a.player().health == Catch::Approx(a.stage.tuning().maxHealth));
}

TEST_CASE("without line of sight the artillery never aims, and with it the same scene does")
{
    ArtilleryArena open(12.0);
    open.tick(60 * 8);
    CHECK(open.brain().shots >= 1);

    ArtilleryArena blocked(12.0);
    Stage0::Ground ground;
    ground.lineOfSight = [](glm::dvec3, glm::dvec3) { return false; };
    blocked.stage.setGround(ground);
    blocked.stage.resetFight(blocked.scene);
    blocked.tick(60 * 8);
    CHECK(blocked.brain().shots == 0);
    CHECK(blocked.player().health == Catch::Approx(blocked.stage.tuning().maxHealth));
}

TEST_CASE("the shell marker sits on the ground the host reports")
{
    ArtilleryArena a(12.0);
    Stage0::Ground ground;
    ground.height = [](double, double) { return 7.0; };
    a.stage.setGround(ground);
    a.stage.resetFight(a.scene);
    REQUIRE(a.tickUntil(ArtilleryState::Aim, 60 * 10));
    std::vector<LineSegment> lines;
    a.stage.debugLines(a.scene, lines);
    int marked = 0;
    for (const LineSegment& line : lines) {
        if (std::abs(line.a.y - 7.1) < 1e-6 && std::abs(line.b.y - 7.1) < 1e-6) ++marked;
    }
    CHECK(marked >= 32);
}

TEST_CASE("the roles draw their own telegraph and not the skirmisher's spacing rings")
{
    AnchorArena anchor(6.0);
    std::vector<LineSegment> idle;
    anchor.stage.debugLines(anchor.scene, idle);
    REQUIRE(anchor.tickUntil(AnchorState::Windup, 60 * 10));
    std::vector<LineSegment> winding;
    anchor.stage.debugLines(anchor.scene, winding);
    CHECK(winding.size() > idle.size());

    Arena skirmisher(FoeRole::Skirmisher, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    std::vector<LineSegment> rings;
    skirmisher.stage.debugLines(skirmisher.scene, rings);
    CHECK(rings.size() > idle.size());
}

TEST_CASE("a non-finite player position changes nothing and leaves the role body with a finite intent")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    SECTION("anchor")
    {
        AnchorArena a(8.0);
        const AnchorBrain before = a.brain();
        a.playerTransform().position.x = nan;
        a.stage.anchorSystem(a.scene);
        CHECK(a.brain().state == before.state);
        CHECK(a.brain().slams == before.slams);
        const CharacterBody& body = a.scene.registry().get<CharacterBody>(a.foeEntity());
        CHECK(std::isfinite(body.intent.x + body.intent.y + body.intent.z));
    }
    SECTION("artillery")
    {
        ArtilleryArena a(12.0);
        const ArtilleryBrain before = a.brain();
        a.playerTransform().position.z = nan;
        a.stage.artillerySystem(a.scene);
        CHECK(a.brain().state == before.state);
        CHECK(a.brain().shots == before.shots);
        const CharacterBody& body = a.scene.registry().get<CharacterBody>(a.foeEntity());
        CHECK(std::isfinite(body.intent.x + body.intent.y + body.intent.z));
    }
}

TEST_CASE("the same seed plays an anchor fight and an artillery fight the same way twice")
{
    const auto hash = [](FoeRole role) {
        Arena a(role, {0.0, 0.0, 4.0}, {0.0, 0.0, -4.0}, 5);
        a.tick(60 * 10);
        return a.scene.stateHash();
    };
    CHECK(hash(FoeRole::Anchor) == hash(FoeRole::Anchor));
    CHECK(hash(FoeRole::Artillery) == hash(FoeRole::Artillery));
    CHECK(hash(FoeRole::Anchor) != hash(FoeRole::Artillery));
}
