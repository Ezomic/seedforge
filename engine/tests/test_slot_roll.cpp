#include "ichor/data/slot_roll.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace ichor;

namespace {

Axiom make(AxiomSlot slot, const std::string& name)
{
    Axiom axiom;
    axiom.slot = slot;
    axiom.id = std::string(axiomSlotName(slot)) + "." + name;
    axiom.title = name;
    return axiom;
}

std::vector<Axiom> shippingPool(int perSlot = 3)
{
    std::vector<Axiom> pool;
    for (const AxiomSlot slot : {AxiomSlot::Cataclysm, AxiomSlot::Law, AxiomSlot::Powers, AxiomSlot::Pressure, AxiomSlot::Anomaly}) {
        for (int i = 0; i < perSlot; ++i) {
            pool.push_back(make(slot, "n" + std::to_string(i)));
        }
    }
    return pool;
}

std::string ids(const SlotRollResult& result)
{
    std::string text;
    for (const SlotAssignment& a : result.slots) {
        text += a.axiomId + ";";
    }
    return text;
}

}

TEST_CASE("rollSlots fills A to D and shows only A to C", "[slot_roll]")
{
    const std::vector<Axiom> pool = shippingPool();
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        const SlotRollResult result = rollSlots(seed, pool);
        REQUIRE(result.ok());
        REQUIRE(result.slots.size() >= 4);
        REQUIRE(result.slots.size() <= 5);
        for (std::size_t i = 0; i < result.slots.size(); ++i) {
            const SlotAssignment& a = result.slots[i];
            REQUIRE(static_cast<std::size_t>(a.slot) == i);
            REQUIRE(a.axiomId.rfind(std::string(axiomSlotName(a.slot)) + ".", 0) == 0);
            REQUIRE(a.shown == (i < 3));
        }
        REQUIRE(result.find(AxiomSlot::Law) != nullptr);
    }
}

TEST_CASE("rollSlots is deterministic and independent of pool order", "[slot_roll]")
{
    const std::vector<Axiom> pool = shippingPool();
    std::vector<Axiom> reversed(pool.rbegin(), pool.rend());
    std::vector<Axiom> rotated = pool;
    std::rotate(rotated.begin(), rotated.begin() + 4, rotated.end());
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        const std::string expected = ids(rollSlots(seed, pool));
        REQUIRE(ids(rollSlots(seed, pool)) == expected);
        REQUIRE(ids(rollSlots(seed, reversed)) == expected);
        REQUIRE(ids(rollSlots(seed, rotated)) == expected);
    }
}

TEST_CASE("rollSlots results are pinned so every platform agrees", "[slot_roll]")
{
    const std::vector<Axiom> pool = shippingPool();
    CHECK(ids(rollSlots(0, pool)) == "cataclysm.n2;law.n2;powers.n1;pressure.n0;anomaly.n1;");
    CHECK(ids(rollSlots(1, pool)) == "cataclysm.n2;law.n1;powers.n2;pressure.n2;");
    CHECK(ids(rollSlots(0xC0FFEEull, pool)) == "cataclysm.n0;law.n0;powers.n0;pressure.n2;anomaly.n1;");
    CHECK(ids(rollSlots(std::numeric_limits<std::uint64_t>::max(), pool)) == "cataclysm.n1;law.n2;powers.n0;pressure.n0;anomaly.n1;");
}

TEST_CASE("rollSlots handles the extreme seeds", "[slot_roll]")
{
    const std::vector<Axiom> pool = shippingPool();
    REQUIRE(rollSlots(0, pool).ok());
    REQUIRE(rollSlots(std::numeric_limits<std::uint64_t>::max(), pool).ok());
}

TEST_CASE("rollSlots reports an empty pool precisely", "[slot_roll]")
{
    const SlotRollResult result = rollSlots(7, std::vector<Axiom>{});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error == SlotRollError::NoCandidates);
    CHECK(result.errorSlot == AxiomSlot::Cataclysm);
    CHECK(result.slots.empty());
    CHECK_FALSE(result.message.empty());
}

TEST_CASE("rollSlots names the first slot the pool cannot fill", "[slot_roll]")
{
    std::vector<Axiom> pool = shippingPool();
    std::erase_if(pool, [](const Axiom& a) { return a.slot == AxiomSlot::Powers; });
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        const SlotRollResult result = rollSlots(seed, pool);
        REQUIRE(result.error == SlotRollError::NoCandidates);
        CHECK(result.errorSlot == AxiomSlot::Powers);
        CHECK(result.message.find("slot C") != std::string::npos);
    }
}

TEST_CASE("rollSlots with one axiom per category always picks those", "[slot_roll]")
{
    const std::vector<Axiom> pool = shippingPool(1);
    int anomalies = 0;
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        const SlotRollResult result = rollSlots(seed, pool);
        REQUIRE(result.ok());
        for (const SlotAssignment& a : result.slots) {
            CHECK(a.axiomId == std::string(axiomSlotName(a.slot)) + ".n0");
        }
        anomalies += result.find(AxiomSlot::Anomaly) != nullptr ? 1 : 0;
    }
    CHECK(anomalies > 0);
    CHECK(anomalies < 300);
}

TEST_CASE("rollSlots rolls no Anomaly when none is authored", "[slot_roll]")
{
    std::vector<Axiom> pool = shippingPool();
    std::erase_if(pool, [](const Axiom& a) { return a.slot == AxiomSlot::Anomaly; });
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        const SlotRollResult result = rollSlots(seed, pool);
        REQUIRE(result.ok());
        CHECK(result.slots.size() == 4);
    }
}

TEST_CASE("rollSlots rejects duplicate ids", "[slot_roll]")
{
    std::vector<Axiom> pool = shippingPool();
    pool.push_back(pool[4]);
    const SlotRollResult result = rollSlots(1, pool);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error == SlotRollError::DuplicateId);
    CHECK(result.message.find(pool[4].id) != std::string::npos);
    CHECK(result.slots.empty());
}

TEST_CASE("rollSlots terminates with an error when the predicate rejects everything", "[slot_roll]")
{
    const std::vector<Axiom> pool = shippingPool();
    const AxiomCoexist never = [](const Axiom&, const Axiom&) { return false; };
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        const SlotRollResult result = rollSlots(seed, pool, never);
        REQUIRE_FALSE(result.ok());
        CHECK(result.error == SlotRollError::AllRejected);
        CHECK(result.errorSlot == AxiomSlot::Law);
        CHECK(result.slots.empty());
    }
}

TEST_CASE("rollSlots drops the optional Anomaly when it clashes with everything", "[slot_roll]")
{
    const std::vector<Axiom> pool = shippingPool();
    const AxiomCoexist noAnomaly = [](const Axiom&, const Axiom& candidate) { return candidate.slot != AxiomSlot::Anomaly; };
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        const SlotRollResult result = rollSlots(seed, pool, noAnomaly);
        REQUIRE(result.ok());
        CHECK(result.slots.size() == 4);
    }
}

TEST_CASE("rollSlots keeps clashing pairs apart and leaves other slots alone", "[slot_roll]")
{
    const std::vector<Axiom> pool = shippingPool();
    const AxiomCoexist noPair = [](const Axiom& chosen, const Axiom& candidate) {
        return !(chosen.id == "cataclysm.n0" && candidate.id == "law.n0");
    };
    int blocked = 0;
    for (std::uint64_t seed = 0; seed < 500; ++seed) {
        const SlotRollResult free = rollSlots(seed, pool);
        const SlotRollResult limited = rollSlots(seed, pool, noPair);
        REQUIRE(limited.ok());
        const bool clash = limited.find(AxiomSlot::Cataclysm)->axiomId == "cataclysm.n0" && limited.find(AxiomSlot::Law)->axiomId == "law.n0";
        CHECK_FALSE(clash);
        CHECK(limited.find(AxiomSlot::Cataclysm)->axiomId == free.find(AxiomSlot::Cataclysm)->axiomId);
        CHECK(limited.find(AxiomSlot::Powers)->axiomId == free.find(AxiomSlot::Powers)->axiomId);
        CHECK(limited.find(AxiomSlot::Pressure)->axiomId == free.find(AxiomSlot::Pressure)->axiomId);
        blocked += limited.find(AxiomSlot::Law)->axiomId != free.find(AxiomSlot::Law)->axiomId ? 1 : 0;
    }
    CHECK(blocked > 0);
}

// Loose bounds, since this is a sanity net and not a statistical test: with
// 6000 seeds and three axioms per slot each A-D axiom expects 2000 picks and
// must land in [1500, 2500]; the Anomaly gate expects 2000 and must land in
// [1750, 2250]; each Anomaly axiom expects about 667 and must land in [450, 900].
TEST_CASE("rollSlots reaches every axiom with a roughly even spread", "[slot_roll]")
{
    const std::vector<Axiom> pool = shippingPool();
    constexpr std::uint64_t kSeeds = 6000;
    std::map<std::string, int> counts;
    int anomalies = 0;
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed) {
        const SlotRollResult result = rollSlots(seed, pool);
        REQUIRE(result.ok());
        for (const SlotAssignment& a : result.slots) {
            ++counts[a.axiomId];
        }
        anomalies += result.find(AxiomSlot::Anomaly) != nullptr ? 1 : 0;
    }
    CHECK(anomalies >= 1750);
    CHECK(anomalies <= 2250);
    for (const Axiom& axiom : pool) {
        const int count = counts[axiom.id];
        INFO(axiom.id << " " << count);
        if (axiom.slot == AxiomSlot::Anomaly) {
            CHECK(count >= 450);
            CHECK(count <= 900);
        } else {
            CHECK(count >= 1500);
            CHECK(count <= 2500);
        }
    }
}
