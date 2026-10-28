#include "ichor/data/world_config.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr AxiomSlot kSlots[] = {AxiomSlot::Cataclysm, AxiomSlot::Law, AxiomSlot::Powers, AxiomSlot::Pressure, AxiomSlot::Anomaly};

Axiom make(AxiomSlot slot, const std::string& name)
{
    Axiom axiom;
    axiom.slot = slot;
    axiom.id = std::string(axiomSlotName(slot)) + "." + name;
    axiom.title = name;
    axiom.blocks["terrain"]["tag_" + axiom.id] = axiom.id;
    return axiom;
}

std::vector<Axiom> syntheticPool()
{
    std::vector<Axiom> pool;
    for (const AxiomSlot slot : kSlots) {
        for (int i = 0; i < 3; ++i) {
            pool.push_back(make(slot, "n" + std::to_string(i)));
        }
    }
    return pool;
}

struct Shipped {
    AxiomSet set;
    std::vector<Axiom> pool;
    ConflictList blocklist;
};

const Shipped& shipped()
{
    static const Shipped data = [] {
        Shipped out;
        REQUIRE(out.set.loadDirectory(ICHOR_GAME_DATA_DIR));
        for (const auto& [id, axiom] : out.set.axioms()) {
            out.pool.push_back(axiom);
        }
        REQUIRE(out.blocklist.loadDataRoot(ICHOR_GAME_DATA_DIR, out.set));
        return out;
    }();
    return data;
}

bool sameSlot(const Axiom& a, const Axiom& b)
{
    return a.slot == b.slot;
}

bool declaredConflict(const Axiom& a, const Axiom& b)
{
    for (const std::string& id : a.conflicts) {
        if (id == b.id) {
            return true;
        }
    }
    for (const std::string& id : b.conflicts) {
        if (id == a.id) {
            return true;
        }
    }
    return false;
}

}

TEST_CASE("a default WorldConfig has no axioms and empty blocks", "[world_config]")
{
    const WorldConfig none;
    CHECK(none.slots.empty());
    CHECK(none.merged.directives.blocks.empty());
    CHECK(worldConfigIds(none).empty());
    CHECK(worldConfigCanonicalText(none) == "|" + mergedCanonicalText(MergeResult{}));
}

TEST_CASE("rollWorldConfig takes rollSlots' axioms and merges their directives", "[world_config]")
{
    const std::vector<Axiom> pool = syntheticPool();
    const ConflictList none;
    bool sawFive = false;
    bool sawFour = false;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const WorldConfigResult result = rollWorldConfig(seed, pool, none);
        REQUIRE(result.ok());
        const SlotRollResult roll = rollSlots(seed, pool);
        REQUIRE(result.config.slots.size() == roll.slots.size());
        std::vector<Axiom> picked;
        for (std::size_t i = 0; i < roll.slots.size(); ++i) {
            CHECK(result.config.slots[i].axiomId == roll.slots[i].axiomId);
            CHECK(result.config.slots[i].slot == roll.slots[i].slot);
            CHECK(result.config.slots[i].shown == roll.slots[i].shown);
            for (const Axiom& a : pool) {
                if (a.id == roll.slots[i].axiomId) {
                    picked.push_back(a);
                }
            }
        }
        CHECK(mergedDigest(result.config.merged) == mergedDigest(mergeDirectives(picked)));
        sawFive = sawFive || result.config.slots.size() == 5;
        sawFour = sawFour || result.config.slots.size() == 4;
    }
    CHECK(sawFive);
    CHECK(sawFour);
}

TEST_CASE("a seed that rolls every slot carries all five axioms with their provenance", "[world_config]")
{
    const WorldConfigResult result = rollWorldConfig(0, syntheticPool(), ConflictList{});
    REQUIRE(result.ok());
    REQUIRE(result.config.slots.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(result.config.slots[i].slot == kSlots[i]);
    }
    std::set<AxiomSlot> sources;
    for (const FieldSource& field : result.config.merged.provenance) {
        sources.insert(field.slots.begin(), field.slots.end());
    }
    CHECK(sources.size() == 5);
}

TEST_CASE("the WorldConfig of a seed is pinned so every platform agrees", "[world_config]")
{
    const std::vector<Axiom> pool = syntheticPool();
    const ConflictList none;
    const auto ids = [&](std::uint64_t seed) { return worldConfigIds(rollWorldConfig(seed, pool, none).config); };
    const auto digest = [&](std::uint64_t seed) { return worldConfigDigest(rollWorldConfig(seed, pool, none).config); };
    CHECK(ids(0) == "cataclysm.n2,law.n2,powers.n1,pressure.n0,anomaly.n1");
    CHECK(ids(1) == "cataclysm.n2,law.n1,powers.n2,pressure.n2");
    CHECK(ids(std::numeric_limits<std::uint64_t>::max()) == "cataclysm.n1,law.n2,powers.n0,pressure.n0,anomaly.n1");
    CHECK(digest(0) == 0x8db0a7352ab10d1cull);
    CHECK(digest(1) == 0x95812a68bdcd245aull);
    CHECK(digest(std::numeric_limits<std::uint64_t>::max()) == 0x6bdcab353c064e26ull);
    CHECK(worldConfigDigest(WorldConfig{}) == 0xf755d131393809a1ull);
}

TEST_CASE("rolling the shipped axioms reaches every axiom and never pairs an excluded one", "[world_config]")
{
    const Shipped& data = shipped();
    std::set<std::string> reached;
    for (std::uint64_t seed = 0; seed < 2000; ++seed) {
        const WorldConfigResult result = rollWorldConfig(seed, data.pool, data.blocklist);
        REQUIRE(result.ok());
        REQUIRE(result.config.slots.size() >= 4);
        std::vector<const Axiom*> picked;
        for (const SlotAssignment& slot : result.config.slots) {
            reached.insert(slot.axiomId);
            picked.push_back(data.set.find(slot.axiomId));
            REQUIRE(picked.back() != nullptr);
        }
        for (std::size_t i = 0; i < picked.size(); ++i) {
            for (std::size_t j = i + 1; j < picked.size(); ++j) {
                REQUIRE_FALSE(sameSlot(*picked[i], *picked[j]));
                REQUIRE_FALSE(declaredConflict(*picked[i], *picked[j]));
                REQUIRE(data.blocklist.mayCoexist(*picked[i], *picked[j]));
            }
        }
    }
    CHECK(reached.size() == data.pool.size());
}

TEST_CASE("axiomsMayCoexist excludes a shared slot, a declared conflict either way and a blocklisted pair", "[world_config]")
{
    const Shipped& data = shipped();
    const Axiom& flood = *data.set.find("cataclysm.flood");
    const Axiom& grief = *data.set.find("cataclysm.demeters_grief");
    const Axiom& kronos = *data.set.find("cataclysm.kronos");
    const Axiom& league = *data.set.find("powers.league");
    const Axiom& oath = *data.set.find("law.oath");
    CHECK_FALSE(axiomsMayCoexist(flood, grief, data.blocklist));
    CHECK_FALSE(axiomsMayCoexist(grief, flood, data.blocklist));
    CHECK_FALSE(axiomsMayCoexist(flood, kronos, data.blocklist));
    CHECK(axiomsMayCoexist(flood, league, data.blocklist));

    Axiom declares = league;
    declares.conflicts = {"law.oath"};
    CHECK_FALSE(axiomsMayCoexist(declares, oath, data.blocklist));
    CHECK_FALSE(axiomsMayCoexist(oath, declares, data.blocklist));

    ConflictList blocked;
    REQUIRE(blocked.loadText("test", R"({"pairs": [["powers.league", "law.oath"]]})", data.set));
    CHECK_FALSE(axiomsMayCoexist(league, oath, blocked));
    CHECK(axiomsMayCoexist(league, flood, blocked));
}

TEST_CASE("a blocklisted pair is never rolled together", "[world_config]")
{
    const Shipped& data = shipped();
    ConflictList blocked;
    REQUIRE(blocked.loadText("test", R"({"pairs": [["cataclysm.flood", "powers.league"]]})", data.set));
    bool sawFlood = false;
    for (std::uint64_t seed = 0; seed < 2000; ++seed) {
        const WorldConfigResult result = rollWorldConfig(seed, data.pool, blocked);
        REQUIRE(result.ok());
        const std::string ids = worldConfigIds(result.config);
        sawFlood = sawFlood || ids.find("cataclysm.flood") != std::string::npos;
        REQUIRE_FALSE((ids.find("cataclysm.flood") != std::string::npos && ids.find("powers.league") != std::string::npos));
    }
    CHECK(sawFlood);
}

TEST_CASE("pinWorldConfig puts the named axioms in slot order whatever order they are given in", "[world_config]")
{
    const Shipped& data = shipped();
    const WorldConfigResult a = pinWorldConfig("powers.league,cataclysm.flood,law.oath", data.pool, data.blocklist);
    const WorldConfigResult b = pinWorldConfig("law.oath,powers.league,cataclysm.flood", data.pool, data.blocklist);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(worldConfigIds(a.config) == "cataclysm.flood,law.oath,powers.league");
    CHECK(worldConfigDigest(a.config) == worldConfigDigest(b.config));
    CHECK(a.config.slots[0].shown);
    CHECK_FALSE(a.config.merged.directives.blocks.empty());
}

TEST_CASE("pinning a seed's rolled axioms gives that seed's WorldConfig", "[world_config]")
{
    const Shipped& data = shipped();
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        const WorldConfigResult rolled = rollWorldConfig(seed, data.pool, data.blocklist);
        REQUIRE(rolled.ok());
        const WorldConfigResult pinned = pinWorldConfig(worldConfigIds(rolled.config), data.pool, data.blocklist);
        REQUIRE(pinned.ok());
        CHECK(worldConfigCanonicalText(pinned.config) == worldConfigCanonicalText(rolled.config));
    }
}

TEST_CASE("the load forms read the data root and report data that does not load", "[world_config]")
{
    const Shipped& data = shipped();
    const WorldConfigResult rolled = loadRolledWorldConfig(22, ICHOR_GAME_DATA_DIR);
    REQUIRE(rolled.ok());
    CHECK(worldConfigCanonicalText(rolled.config) == worldConfigCanonicalText(rollWorldConfig(22, data.pool, data.blocklist).config));
    const WorldConfigResult pinned = loadPinnedWorldConfig("cataclysm.flood", ICHOR_GAME_DATA_DIR);
    REQUIRE(pinned.ok());
    CHECK(worldConfigIds(pinned.config) == "cataclysm.flood");
    CHECK(loadPinnedWorldConfig("cataclysm.nope", ICHOR_GAME_DATA_DIR).error == "unknown axiom 'cataclysm.nope'");

    const std::filesystem::path missing = std::filesystem::path(ICHOR_GAME_DATA_DIR) / "no_such_data_root";
    CHECK(loadRolledWorldConfig(22, missing).error.find("no axioms directory") != std::string::npos);
    CHECK(loadPinnedWorldConfig("cataclysm.flood", missing).error.find("no axioms directory") != std::string::npos);
}

TEST_CASE("pinWorldConfig refuses bad lists and names the culprit", "[world_config]")
{
    const Shipped& data = shipped();
    const auto error = [&](std::string_view ids) { return pinWorldConfig(ids, data.pool, data.blocklist).error; };
    CHECK(error("") == "no axiom ids given");
    CHECK(error("cataclysm.flood,,law.oath") == "an empty axiom id in the list");
    CHECK(error("cataclysm.flood,") == "an empty axiom id in the list");
    CHECK(error("cataclysm.nope") == "unknown axiom 'cataclysm.nope'");
    CHECK(error("cataclysm.flood,cataclysm.flood") == "axiom 'cataclysm.flood' is named twice");
    CHECK(error("cataclysm.flood,cataclysm.kronos") == "axiom 'cataclysm.kronos' may not share a world with 'cataclysm.flood'");
    CHECK(error("cataclysm.demeters_grief,cataclysm.flood") == "axiom 'cataclysm.flood' may not share a world with 'cataclysm.demeters_grief'");

    ConflictList blocked;
    REQUIRE(blocked.loadText("test", R"({"pairs": [["powers.league", "law.oath"]]})", data.set));
    CHECK(pinWorldConfig("law.oath,powers.league", data.pool, blocked).error == "axiom 'powers.league' may not share a world with 'law.oath'");
    CHECK(pinWorldConfig("law.oath,powers.league", data.pool, blocked).config.slots.empty());
}
