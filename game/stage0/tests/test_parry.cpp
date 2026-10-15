#include "ichor/data/data_set.h"
#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/weapon.h"

#include <catch2/catch_test_macros.hpp>

using namespace ichor;
using namespace ichor::stage0;

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr float kBlow = 30.0f;
constexpr float kBlowPoise = 40.0f;

InputFrame blockHeld()
{
    InputFrame frame;
    frame.buttons = InputFrame::Block;
    return frame;
}

struct Arena {
    Scene scene;
    Stage0 stage;
    explicit Arena(WeaponProfile playerWeapon, std::uint64_t seed = 3)
        : scene(seed)
    {
        stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
        scene.tick(kDt);
        stage.resetFight(scene);
        for (int i = 0; i < 30; ++i) {
            scene.tick(kDt);
        }
        stage.setWeapon(Side::Player, playerWeapon);
    }
    entt::entity playerEntity() { return scene.find(stage.playerId()); }
    entt::entity foeEntity() { return scene.find(stage.foeId()); }
    Combatant& player() { return scene.registry().get<Combatant>(playerEntity()); }
    Combatant& foe() { return scene.registry().get<Combatant>(foeEntity()); }

    void holdBlock(int ticks)
    {
        for (int i = 0; i < ticks; ++i) {
            scene.tick(kDt, blockHeld());
        }
    }
    void foeStrikes() { stage.receiveHit(scene, playerEntity(), foeEntity(), kBlow, kBlowPoise, true); }
};

WeaponProfile parrySword()
{
    WeaponProfile weapon;
    weapon.parryWindowTicks = 6;
    weapon.parryPoiseDamage = 100.0f;
    return weapon;
}

}

TEST_CASE("a weapon parries nothing unless its record says so")
{
    REQUIRE_FALSE(WeaponProfile{}.parries());
    REQUIRE(parrySword().parries());
}

TEST_CASE("a weapon without parry never parries, at any timing")
{
    for (int held = 1; held <= 8; ++held) {
        Arena a(WeaponProfile{});
        a.holdBlock(held);
        a.foeStrikes();
        REQUIRE(a.player().health == 100.0f - kBlow * 0.4f);
        REQUIRE(a.foe().combatState() != CombatState::Staggered);
    }
}

TEST_CASE("a parry weapon deflects inside the window and staggers the attacker")
{
    Arena a(parrySword());
    a.holdBlock(3);
    const float stamina = a.player().stamina;
    a.foeStrikes();
    REQUIRE(a.player().health == 100.0f);
    REQUIRE(a.player().stamina == stamina);
    REQUIRE(a.foe().combatState() == CombatState::Staggered);
    REQUIRE(a.foe().poise == 0.0f);
}

TEST_CASE("the window opens on the first held tick and closes after its last")
{
    for (int held = 1; held <= 9; ++held) {
        Arena a(parrySword());
        a.holdBlock(held);
        a.foeStrikes();
        const bool parried = a.foe().combatState() == CombatState::Staggered;
        REQUIRE(parried == (held <= 6));
        REQUIRE((a.player().health == 100.0f) == parried);
    }
}

TEST_CASE("a parry needs a raised guard, so a bare hit is never parried")
{
    Arena a(parrySword());
    a.scene.tick(kDt);
    a.foeStrikes();
    REQUIRE(a.player().health == 100.0f - kBlow);
    REQUIRE(a.foe().combatState() != CombatState::Staggered);
}

TEST_CASE("a parry that leaves the attacker some poise does not stagger it")
{
    WeaponProfile weapon = parrySword();
    weapon.parryPoiseDamage = 30.0f;
    Arena a(weapon);
    a.holdBlock(2);
    a.foeStrikes();
    REQUIRE(a.player().health == 100.0f);
    REQUIRE(a.foe().combatState() != CombatState::Staggered);
    REQUIRE(a.foe().poise == 70.0f);
}

TEST_CASE("two runs of the same parry timing end in the same state")
{
    const auto run = [] {
        Arena a(parrySword());
        a.holdBlock(5);
        a.foeStrikes();
        for (int i = 0; i < 20; ++i) {
            a.scene.tick(kDt, blockHeld());
        }
        return a.scene.stateHash();
    };
    REQUIRE(run() == run());
}

TEST_CASE("the parry property is read from a weapon record and defaults to off")
{
    DataRecord plain;
    plain.id = "iron_sword";
    plain.fields["damage"] = 5.0;
    REQUIRE_FALSE(weaponFromRecord(plain).parries());

    DataRecord parry = plain;
    parry.id = "main_gauche";
    parry.fields["parry_window_ticks"] = 8.0;
    parry.fields["parry_poise_damage"] = 55.0;
    const WeaponProfile weapon = weaponFromRecord(parry);
    REQUIRE(weapon.parries());
    REQUIRE(weapon.parryWindowTicks == 8);
    REQUIRE(weapon.parryPoiseDamage == 55.0f);
}

TEST_CASE("a malformed parry record turns parry off instead of inventing a window")
{
    DataRecord record;
    record.id = "odd";
    record.fields["parry_window_ticks"] = std::string("wide");
    REQUIRE_FALSE(weaponFromRecord(record).parries());
    record.fields["parry_window_ticks"] = -4.0;
    REQUIRE_FALSE(weaponFromRecord(record).parries());
    record.fields["parry_window_ticks"] = 1.0e9;
    REQUIRE(weaponFromRecord(record).parryWindowTicks == kMaxParryWindowTicks);
}
