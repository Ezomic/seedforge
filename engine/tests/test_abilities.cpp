#include "ichor/progression/abilities.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using namespace ichor;

namespace {

constexpr std::int64_t kI64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kI64Min = std::numeric_limits<std::int64_t>::min();

struct RecordingSource final : PowerSource {
    int payCalls = 0;
    std::int64_t lastCost = 0;
    bool canPay(std::int64_t) const override { return true; }
    bool pay(std::int64_t cost) override
    {
        ++payCalls;
        lastCost = cost;
        return true;
    }
};

const AbilityDef kBolt{"bolt", 3, 40};

}

TEST_CASE("an ability pays through whatever source it is given", "[ability]")
{
    RecordingSource recorder;
    const AbilityResult r = resolveAbility(kBolt, recorder);
    CHECK(r.outcome == AbilityOutcome::Resolved);
    CHECK(r.effect == 40);
    CHECK(recorder.payCalls == 1);
    CHECK(recorder.lastCost == 3);
}

TEST_CASE("swapping the source changes only what pays, not what the ability does", "[ability]")
{
    ChargeSource charges(10);
    HealthSource health(10);
    const AbilityResult a = resolveAbility(kBolt, charges);
    const AbilityResult b = resolveAbility(kBolt, health);
    CHECK(a.outcome == AbilityOutcome::Resolved);
    CHECK(b.outcome == AbilityOutcome::Resolved);
    CHECK(a.effect == b.effect);
    CHECK(charges.charges() == 7);
    CHECK(health.health() == 7);
}

TEST_CASE("the same ability is affordable on one source and not another", "[ability]")
{
    ChargeSource charges(3);
    HealthSource health(3);
    CHECK(resolveAbility(kBolt, charges).outcome == AbilityOutcome::Resolved);
    const AbilityResult r = resolveAbility(kBolt, health);
    CHECK(r.outcome == AbilityOutcome::Unaffordable);
    CHECK(r.effect == 0);
    CHECK(health.health() == 3);
}

TEST_CASE("a use that cannot be paid changes nothing", "[ability]")
{
    ChargeSource charges(2);
    CHECK(resolveAbility(kBolt, charges).outcome == AbilityOutcome::Unaffordable);
    CHECK(charges.charges() == 2);
}

TEST_CASE("hecatomb never kills the caster", "[ability][hecatomb]")
{
    HealthSource health(4);
    CHECK(resolveAbility(AbilityDef{"a", 4, 1}, health).outcome == AbilityOutcome::Unaffordable);
    CHECK(health.health() == 4);
    CHECK(resolveAbility(AbilityDef{"a", 3, 1}, health).outcome == AbilityOutcome::Resolved);
    CHECK(health.health() == 1);
    CHECK(resolveAbility(AbilityDef{"a", 1, 1}, health).outcome == AbilityOutcome::Unaffordable);
    CHECK(health.health() == 1);
}

TEST_CASE("orichalcum spends charges down to zero", "[ability][orichalcum]")
{
    ChargeSource charges(3);
    CHECK(resolveAbility(kBolt, charges).outcome == AbilityOutcome::Resolved);
    CHECK(charges.charges() == 0);
    CHECK(resolveAbility(AbilityDef{"a", 1, 1}, charges).outcome == AbilityOutcome::Unaffordable);
}

TEST_CASE("hostile definitions never reach the source", "[ability]")
{
    RecordingSource recorder;
    const AbilityDef bad[] = {
        {"", 1, 1},
        {"a", 0, 1},
        {"a", -1, 1},
        {"a", kI64Min, 1},
        {"a", kI64Max, 1},
        {"a", kMaxAbilityCost + 1, 1},
        {"a", 1, -1},
        {"a", 1, kMaxAbilityEffect + 1},
        {"a", 1, kI64Max},
    };
    for (const AbilityDef& d : bad) {
        CHECK(resolveAbility(d, recorder).outcome == AbilityOutcome::InvalidDef);
    }
    CHECK(recorder.payCalls == 0);
}

TEST_CASE("sources reject hostile costs and starting values", "[ability]")
{
    ChargeSource charges(kI64Max);
    CHECK_FALSE(charges.pay(0));
    CHECK_FALSE(charges.pay(-5));
    CHECK_FALSE(charges.pay(kI64Min));
    CHECK(charges.charges() == kI64Max);
    HealthSource neg(-9);
    CHECK(neg.health() == 0);
    CHECK_FALSE(neg.canPay(1));
    ChargeSource negCharges(kI64Min);
    CHECK(negCharges.charges() == 0);
}

TEST_CASE("resolution is deterministic", "[ability]")
{
    ChargeSource a(20), b(20);
    for (int i = 0; i < 5; ++i) {
        CHECK(resolveAbility(kBolt, a).outcome == resolveAbility(kBolt, b).outcome);
    }
    CHECK(a.charges() == b.charges());
}
