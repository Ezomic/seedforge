#include "ichor/progression/abilities.h"
#include "ichor/progression/last_favour.h"
#include "ichor/progression/law_power_sources.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

constexpr std::int64_t kI64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kI64Min = std::numeric_limits<std::int64_t>::min();

// The Oath maps onto the interface directly: paying is borrowing against the ceiling.
class OathAdapter final : public PowerSource {
public:
    explicit OathAdapter(OathDebt& oath)
        : oath_(oath)
    {
    }
    bool canPay(std::int64_t cost) const override { return cost >= 1 && oath_.debt <= oath_.ceiling && cost <= oath_.ceiling - oath_.debt; }
    bool pay(std::int64_t cost) override { return oathBorrow(oath_, cost); }

private:
    OathDebt& oath_;
};

// The Mysteries do NOT map directly: a use is keyed by rite id, and PowerSource::pay carries only
// a cost. The adapter has to be bound to one rite, so one source per ability, and the cost is
// ignored (a rite is one use). This is the interface gap reported in the PR.
class MysteriesRiteAdapter final : public PowerSource {
public:
    MysteriesRiteAdapter(MysteriesLoadout& loadout, std::string riteId)
        : loadout_(loadout)
        , riteId_(std::move(riteId))
    {
    }
    bool canPay(std::int64_t cost) const override
    {
        if (cost < 1)
            return false;
        for (const Rite& r : loadout_.rites) {
            if (r.id == riteId_)
                return !r.revoked && r.usesLeft > 0;
        }
        return false;
    }
    bool pay(std::int64_t cost) override { return canPay(cost) && mysteriesCast(loadout_, riteId_); }

private:
    MysteriesLoadout& loadout_;
    std::string riteId_;
};

const AbilityDef kBolt{"bolt", 3, 40};

}

TEST_CASE("the last favour never refills and counts what was spent", "[ability][lastfavour]")
{
    LastFavourSource favour(7);
    CHECK(resolveAbility(kBolt, favour).outcome == AbilityOutcome::Resolved);
    CHECK(resolveAbility(kBolt, favour).outcome == AbilityOutcome::Resolved);
    CHECK(favour.charges() == 1);
    CHECK(favour.spent() == 6);
    CHECK(resolveAbility(kBolt, favour).outcome == AbilityOutcome::Unaffordable);
    CHECK(favour.charges() == 1);
    CHECK(favour.spent() == 6);
    CHECK(favour.charges() + favour.spent() == 7);
}

TEST_CASE("efficiency makes the same ability cheaper without adding charges", "[ability][lastfavour]")
{
    LastFavourSource plain(100), thrifty(100);
    CHECK(thrifty.improveEfficiency(300) == 300);
    resolveAbility(AbilityDef{"a", 10, 1}, plain);
    resolveAbility(AbilityDef{"a", 10, 1}, thrifty);
    CHECK(plain.charges() == 90);
    CHECK(thrifty.charges() == 93);
    CHECK(thrifty.charges() + thrifty.spent() == 100);
}

TEST_CASE("efficiency is capped and a favour is never free", "[ability][lastfavour]")
{
    LastFavourSource favour(10);
    CHECK(favour.improveEfficiency(kI64Max) == kMaxFavourDiscountPermille);
    CHECK(favour.improveEfficiency(1) == 0);
    CHECK(favour.improveEfficiency(0) == 0);
    CHECK(favour.improveEfficiency(-50) == 0);
    CHECK(favour.improveEfficiency(kI64Min) == 0);
    CHECK(favour.discountPermille() == kMaxFavourDiscountPermille);
    CHECK(favour.effectiveCost(1) == 1);
    CHECK(favour.effectiveCost(3) == 2);
    CHECK(favour.effectiveCost(kMaxAbilityCost) == kMaxAbilityCost / 2);
}

TEST_CASE("last favour rejects hostile costs and starting values", "[ability][lastfavour]")
{
    LastFavourSource favour(5);
    for (const std::int64_t c : {std::int64_t{0}, std::int64_t{-1}, kI64Min, kI64Max, kMaxAbilityCost + 1}) {
        CHECK_FALSE(favour.canPay(c));
        CHECK_FALSE(favour.pay(c));
        CHECK(favour.effectiveCost(c) == 0);
    }
    CHECK(favour.charges() == 5);
    CHECK(LastFavourSource(kI64Min).charges() == 0);
    CHECK(LastFavourSource(kI64Max).charges() == kMaxFavourCharges);
    CHECK_FALSE(LastFavourSource(0).canPay(1));
}

TEST_CASE("one ability list runs unchanged on every source", "[ability][lastfavour][swap]")
{
    const std::vector<AbilityDef> kit = {{"bolt", 3, 40}, {"ward", 2, 15}, {"smite", 5, 90}};

    ChargeSource orichalcum(20);
    HealthSource hecatomb(20);
    LastFavourSource favour(20);
    OathDebt oath{0, 20};
    OathAdapter oathSource(oath);
    MysteriesLoadout loadout;
    REQUIRE(mysteriesAdmit(loadout, "bolt", 2, 3));
    REQUIRE(mysteriesAdmit(loadout, "ward", 2, 3));
    REQUIRE(mysteriesAdmit(loadout, "smite", 2, 3));
    MysteriesRiteAdapter boltRite(loadout, "bolt"), wardRite(loadout, "ward"), smiteRite(loadout, "smite");

    std::vector<PowerSource*> flat = {&orichalcum, &hecatomb, &favour, &oathSource};
    for (PowerSource* source : flat) {
        std::int64_t total = 0;
        for (const AbilityDef& def : kit) {
            const AbilityResult r = resolveAbility(def, *source);
            CHECK(r.outcome == AbilityOutcome::Resolved);
            total += r.effect;
        }
        CHECK(total == 145);
    }
    CHECK(orichalcum.charges() == 10);
    CHECK(hecatomb.health() == 10);
    CHECK(favour.charges() == 10);
    CHECK(oath.debt == 10);

    PowerSource* perRite[] = {&boltRite, &wardRite, &smiteRite};
    std::int64_t total = 0;
    for (std::size_t i = 0; i < kit.size(); ++i) {
        const AbilityResult r = resolveAbility(kit[i], *perRite[i]);
        CHECK(r.outcome == AbilityOutcome::Resolved);
        total += r.effect;
    }
    CHECK(total == 145);
    CHECK(loadout.rites[0].usesLeft == 1);
}

TEST_CASE("sources differ only in when they refuse", "[ability][lastfavour][swap]")
{
    const AbilityDef pricey{"pricey", 6, 1};
    ChargeSource orichalcum(6);
    HealthSource hecatomb(6);
    LastFavourSource favour(6);
    CHECK(resolveAbility(pricey, orichalcum).outcome == AbilityOutcome::Resolved);
    CHECK(resolveAbility(pricey, hecatomb).outcome == AbilityOutcome::Unaffordable);
    CHECK(resolveAbility(pricey, favour).outcome == AbilityOutcome::Resolved);
    CHECK(favour.charges() == 0);
    OathDebt oath{5, 6};
    OathAdapter oathSource(oath);
    CHECK(resolveAbility(pricey, oathSource).outcome == AbilityOutcome::Unaffordable);
    CHECK(oath.debt == 5);
}

TEST_CASE("a revoked or spent rite refuses through the same entry point", "[ability][lastfavour][swap]")
{
    MysteriesLoadout loadout;
    REQUIRE(mysteriesAdmit(loadout, "bolt", 1, 2));
    MysteriesRiteAdapter rite(loadout, "bolt");
    CHECK(resolveAbility(kBolt, rite).outcome == AbilityOutcome::Resolved);
    CHECK(resolveAbility(kBolt, rite).outcome == AbilityOutcome::Unaffordable);
    MysteriesRiteAdapter missing(loadout, "nope");
    CHECK(resolveAbility(kBolt, missing).outcome == AbilityOutcome::Unaffordable);
}

TEST_CASE("last favour resolution is deterministic", "[ability][lastfavour]")
{
    LastFavourSource a(50), b(50);
    a.improveEfficiency(250);
    b.improveEfficiency(250);
    for (int i = 0; i < 30; ++i)
        CHECK(resolveAbility(kBolt, a).outcome == resolveAbility(kBolt, b).outcome);
    CHECK(a.charges() == b.charges());
    CHECK(a.spent() == b.spent());
}
