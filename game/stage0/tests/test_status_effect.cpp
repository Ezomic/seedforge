#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/status_effect.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ichor;
using namespace ichor::stage0;

namespace {

constexpr double kDt = 1.0 / 60.0;

struct Arena {
    Scene scene;
    Stage0 stage;
    explicit Arena(std::uint64_t seed = 1)
        : scene(seed)
    {
        stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -8.0});
        scene.tick(kDt);
        stage.resetFight(scene);
        for (int i = 0; i < 30; ++i) {
            scene.tick(kDt);
        }
    }
    entt::entity playerEntity() { return scene.find(stage.playerId()); }
    entt::entity foeEntity() { return scene.find(stage.foeId()); }
    Combatant& player() { return scene.registry().get<Combatant>(playerEntity()); }
    Combatant& foe() { return scene.registry().get<Combatant>(foeEntity()); }
};

InputFrame forward()
{
    InputFrame frame;
    frame.moveY = 1.0f;
    return frame;
}

double walkedZ(Arena& a, int ticks)
{
    const double before = a.scene.registry().get<Transform>(a.playerEntity()).position.z;
    for (int i = 0; i < ticks; ++i) {
        a.scene.tick(kDt, forward());
    }
    return before - a.scene.registry().get<Transform>(a.playerEntity()).position.z;
}

}

TEST_CASE("bleed does its damage once per tick for its whole duration, then expires")
{
    StatusSet set;
    REQUIRE(set.apply(StatusId::Bleed, 3));
    REQUIRE(set.tick() == 400);
    REQUIRE(set.tick() == 400);
    REQUIRE(set.find(StatusId::Bleed) != nullptr);
    REQUIRE(set.tick() == 400);
    REQUIRE(set.empty());
    REQUIRE(set.tick() == 0);
}

TEST_CASE("an effect of one tick bites once on the tick after it lands")
{
    StatusSet set;
    REQUIRE(set.apply(StatusId::Bleed, 1));
    REQUIRE(set.tick() == 400);
    REQUIRE(set.empty());
}

TEST_CASE("zero, negative and unknown inputs apply nothing; a huge duration is clamped")
{
    StatusSet set;
    REQUIRE_FALSE(set.apply(StatusId::Bleed, -5));
    REQUIRE_FALSE(set.apply(StatusId::None));
    REQUIRE_FALSE(set.apply(static_cast<StatusId>(200)));
    REQUIRE(set.empty());

    REQUIRE(set.apply(StatusId::Slow, std::numeric_limits<std::int32_t>::max()));
    REQUIRE(set.find(StatusId::Slow)->remainingTicks == kMaxStatusDurationTicks);

    REQUIRE(set.apply(StatusId::Bleed)); // zero means the definition's own duration
    REQUIRE(set.find(StatusId::Bleed)->remainingTicks == statusDef(StatusId::Bleed)->durationTicks);
}

TEST_CASE("applying bleed every tick holds at the stack cap and keeps the effect alive")
{
    StatusSet set;
    const StatusDef& def = *statusDef(StatusId::Bleed);
    for (int i = 0; i < 10000; ++i) {
        REQUIRE(set.apply(StatusId::Bleed));
        set.tick();
    }
    REQUIRE(set.find(StatusId::Bleed)->stacks == def.maxStacks);
    REQUIRE(set.effects.size() == 1);
    REQUIRE(set.tick() == static_cast<std::int64_t>(def.damageMilliPerTick) * def.maxStacks);
}

TEST_CASE("refresh resets the duration without adding stacks")
{
    StatusSet set;
    set.apply(StatusId::Slow, 10);
    set.tick();
    set.tick();
    set.apply(StatusId::Slow, 10);
    REQUIRE(set.find(StatusId::Slow)->remainingTicks == 10);
    REQUIRE(set.find(StatusId::Slow)->stacks == 1);
    set.apply(StatusId::Slow, 1);
    REQUIRE(set.find(StatusId::Slow)->remainingTicks == 10); // a weaker reapply never shortens
    REQUIRE(set.moveScale() == Catch::Approx(0.65f));
}

TEST_CASE("corrupt loaded state is pulled back into range instead of overflowing")
{
    StatusSet set;
    set.effects.push_back({static_cast<std::uint8_t>(StatusId::Bleed), std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min()});
    set.effects.push_back({static_cast<std::uint8_t>(StatusId::Slow), -4, std::numeric_limits<std::int32_t>::max()});
    REQUIRE(set.tick() == 400LL * statusDef(StatusId::Bleed)->maxStacks);
    REQUIRE(set.find(StatusId::Bleed) == nullptr);
    REQUIRE(set.find(StatusId::Slow)->stacks == 1);
    REQUIRE(set.find(StatusId::Slow)->remainingTicks == kMaxStatusDurationTicks - 1);
}

TEST_CASE("effects stay sorted by id however they were applied")
{
    StatusSet a;
    a.apply(StatusId::Slow);
    a.apply(StatusId::Bleed);
    StatusSet b;
    b.apply(StatusId::Bleed);
    b.apply(StatusId::Slow);
    REQUIRE(a.effects.size() == 2);
    REQUIRE(a.effects[0].id == static_cast<std::uint8_t>(StatusId::Bleed));
    REQUIRE(a.effects[1].id == static_cast<std::uint8_t>(StatusId::Slow));
    for (int i = 0; i < 5; ++i) {
        REQUIRE(a.tick() == b.tick());
    }
    REQUIRE(a.effects[0].remainingTicks == b.effects[0].remainingTicks);
}

TEST_CASE("bleed drains health on a combatant and saturates at zero")
{
    Arena a;
    REQUIRE(a.stage.applyStatus(a.scene, a.playerEntity(), StatusId::Bleed, 10));
    a.scene.tick(kDt);
    REQUIRE(a.player().health == Catch::Approx(99.6f).margin(1e-4));
    for (int i = 0; i < 20; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE(a.player().health == Catch::Approx(96.0f).margin(1e-3)); // ten ticks, no more
    REQUIRE(a.player().status.empty());
}

TEST_CASE("bleed can kill, stamps its own death cause, and nothing can be applied to the dead")
{
    Arena a;
    a.foe().health = 0.5f;
    REQUIRE(a.stage.applyStatus(a.scene, a.foeEntity(), StatusId::Bleed, 100));
    for (int i = 0; i < 3; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE_FALSE(a.foe().alive());
    REQUIRE(a.foe().health == 0.0f);
    REQUIRE(a.foe().deathCause == static_cast<std::uint8_t>(DeathCause::Bleed));
    REQUIRE_FALSE(a.stage.applyStatus(a.scene, a.foeEntity(), StatusId::Slow));
    const float health = a.player().health;
    for (int i = 0; i < 5; ++i) {
        a.scene.tick(kDt);
    }
    REQUIRE(a.foe().health == 0.0f);
    REQUIRE(a.player().health == health);
}

TEST_CASE("slow cuts the player's walking distance by its share")
{
    Arena plain;
    Arena slowed;
    slowed.stage.applyStatus(slowed.scene, slowed.playerEntity(), StatusId::Slow);
    const double normal = walkedZ(plain, 20);
    const double slow = walkedZ(slowed, 20);
    REQUIRE(normal > 1.0);
    REQUIRE(slow == Catch::Approx(normal * 0.65).epsilon(0.05));
}

TEST_CASE("slow cuts the skirmisher's approach speed")
{
    Arena plain;
    Arena slowed;
    slowed.stage.applyStatus(slowed.scene, slowed.foeEntity(), StatusId::Slow, 1000);
    for (int i = 0; i < 90; ++i) {
        plain.scene.tick(kDt);
        slowed.scene.tick(kDt);
    }
    const double plainZ = plain.scene.registry().get<Transform>(plain.foeEntity()).position.z;
    const double slowZ = slowed.scene.registry().get<Transform>(slowed.foeEntity()).position.z;
    REQUIRE(plainZ - -8.0 > 0.5);
    REQUIRE((slowZ - -8.0) < (plainZ - -8.0) * 0.8);
}

TEST_CASE("status effects are part of the state hash and replay identically")
{
    const auto run = [](bool bleed) {
        Arena a(7);
        if (bleed) a.stage.applyStatus(a.scene, a.playerEntity(), StatusId::Bleed, 50);
        a.stage.applyStatus(a.scene, a.foeEntity(), StatusId::Slow, 50);
        for (int i = 0; i < 120; ++i) {
            a.scene.tick(kDt);
        }
        return a.scene.stateHash();
    };
    REQUIRE(run(true) == run(true));
    REQUIRE(run(true) != run(false));
}

TEST_CASE("a status survives a mid-effect hash comparison: the remaining duration is hashed")
{
    Arena a(3);
    Arena b(3);
    a.stage.applyStatus(a.scene, a.playerEntity(), StatusId::Slow, 100);
    b.stage.applyStatus(b.scene, b.playerEntity(), StatusId::Slow, 101);
    a.scene.tick(kDt);
    b.scene.tick(kDt);
    REQUIRE(a.scene.stateHash() != b.scene.stateHash());
}

TEST_CASE("a fresh fight clears every status")
{
    Arena a;
    a.stage.applyStatus(a.scene, a.playerEntity(), StatusId::Bleed);
    a.stage.resetFight(a.scene);
    REQUIRE(a.player().status.empty());
}
