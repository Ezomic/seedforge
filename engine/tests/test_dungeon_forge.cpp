#include "ichor/graph/dungeon_forge.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace ichor::graph;
using namespace ichor::graph::forge;
namespace m = ichor::graph::mission;

namespace {

const m::Ruleset& rules()
{
    static const m::Ruleset r = m::loadRuleset(std::string(ICHOR_REPO_DATA) + "/dungeon_missions/mission_rules.json");
    return r;
}

Record forged(std::uint64_t seed)
{
    Params p;
    p.seed = seed;
    const ForgeResult r = forgeLayout(rules(), p);
    INFO(r.error);
    REQUIRE(r.error.empty());
    return r.record;
}

}

TEST_CASE("forging is deterministic and instantiation reproduces the layout", "[dungeon_forge]")
{
    for (std::uint64_t seed = 0; seed < 12; ++seed) {
        const Record a = forged(seed);
        CHECK(a == forged(seed));
        const InstanceResult i = instantiate(rules(), a);
        INFO(i.error);
        REQUIRE(i.error.empty());
        CHECK(i.instance.chambers.size() == a.rooms);
        CHECK(layoutHash(i.instance.graph, i.instance.cells) == a.layoutHash);
        const InstanceResult again = instantiate(rules(), a);
        CHECK(again.instance.graph.digest() == i.instance.graph.digest());
    }
}

TEST_CASE("different seeds forge different layouts", "[dungeon_forge]")
{
    std::vector<std::uint64_t> hashes;
    for (std::uint64_t seed = 0; seed < 12; ++seed)
        hashes.push_back(forged(seed).layoutHash);
    std::sort(hashes.begin(), hashes.end());
    CHECK(std::unique(hashes.begin(), hashes.end()) - hashes.begin() >= 6);
}

TEST_CASE("the record round-trips through the codec", "[dungeon_forge]")
{
    const Record r = forged(7);
    const auto bytes = encode(r);
    CHECK(bytes.size() == kEncodedSize);
    const auto back = decode(bytes);
    REQUIRE(back.has_value());
    CHECK(*back == r);
}

TEST_CASE("hostile bytes are rejected without crashing", "[dungeon_forge]")
{
    const auto good = encode(forged(3));
    CHECK_FALSE(decode({}).has_value());
    for (std::size_t n = 0; n < good.size(); ++n)
        CHECK_FALSE(decode(std::span<const std::uint8_t>(good.data(), n)).has_value());
    std::vector<std::uint8_t> longer = good;
    longer.push_back(0);
    CHECK_FALSE(decode(longer).has_value());
    for (std::size_t i = 0; i < good.size(); ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            auto flipped = good;
            flipped[i] ^= static_cast<std::uint8_t>(1u << bit);
            CHECK_FALSE(decode(flipped).has_value());
        }
    }
    std::vector<std::uint8_t> ff(kEncodedSize, 0xFF);
    CHECK_FALSE(decode(ff).has_value());
}

TEST_CASE("a record with a valid checksum but out-of-range fields is rejected", "[dungeon_forge]")
{
    Record r = forged(3);
    r.maxExtent = 0;
    CHECK_FALSE(decode(encode(r)).has_value());
    r = forged(3);
    r.rooms = 0;
    CHECK_FALSE(decode(encode(r)).has_value());
    r = forged(3);
    r.maxFloors = 200;
    CHECK_FALSE(decode(encode(r)).has_value());
}

TEST_CASE("instantiate refuses a tampered or out-of-range record", "[dungeon_forge]")
{
    const Record r = forged(5);
    Record wrongHash = r;
    wrongHash.layoutHash ^= 1;
    CHECK_FALSE(instantiate(rules(), wrongHash).error.empty());
    Record wrongRooms = r;
    wrongRooms.rooms = static_cast<std::uint16_t>(r.rooms + 1);
    CHECK_FALSE(instantiate(rules(), wrongRooms).error.empty());
    Record tooBig = r;
    tooBig.growSteps = 255;
    const InstanceResult res = instantiate(rules(), tooBig);
    CHECK_FALSE(res.error.empty());
    CHECK(res.instance.chambers.empty());
    CHECK_FALSE(instantiate(m::Ruleset{}, r).error.empty());
}

TEST_CASE("forge rejects out-of-range parameters", "[dungeon_forge]")
{
    Params p;
    p.locks = 1000;
    CHECK_FALSE(forgeLayout(rules(), p).error.empty());
    p = Params{};
    p.maxExtent = 0;
    CHECK_FALSE(forgeLayout(rules(), p).error.empty());
    p = Params{};
    CHECK_FALSE(forgeLayout(m::Ruleset{}, p).error.empty());
}

TEST_CASE("chambers are stable references for the quest binder", "[dungeon_forge]")
{
    const Record r = forged(9);
    const InstanceResult a = instantiate(rules(), r);
    const InstanceResult b = instantiate(rules(), r);
    REQUIRE(a.instance.chambers.size() >= 3);
    CHECK(a.instance.chambers[2] == b.instance.chambers[2]);
    CHECK(a.instance.graph.node(a.instance.chambers[2])->type == b.instance.graph.node(b.instance.chambers[2])->type);
}
