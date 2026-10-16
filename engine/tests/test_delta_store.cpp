#include <catch2/catch_test_macros.hpp>

#include "ichor/save/delta_store.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::vector<std::uint8_t> bytes(std::initializer_list<int> v)
{
    std::vector<std::uint8_t> out;
    for (int b : v) {
        out.push_back(static_cast<std::uint8_t>(b));
    }
    return out;
}

DeltaState randomState(std::mt19937& rng, int maxKeys)
{
    DeltaState s;
    const int n = static_cast<int>(rng() % (maxKeys + 1));
    for (int i = 0; i < n; ++i) {
        std::vector<std::uint8_t> v(rng() % 6);
        for (auto& b : v) {
            b = static_cast<std::uint8_t>(rng() % 3);
        }
        s["k" + std::to_string(rng() % 40)] = v;
    }
    return s;
}

}

TEST_CASE("delta of identical states is empty and encodes to a stable minimum", "[save][delta]")
{
    const DeltaState s{{"chest/1", bytes({1})}, {"npc/2", bytes({})}};
    const auto d = computeDelta(s, s);
    CHECK(d.empty());
    CHECK(computeDelta({}, {}).empty());
    const auto enc = encodeDelta(d);
    REQUIRE(enc.has_value());
    CHECK(*enc == bytes({kDeltaFormatVersion, 0, 0}));
    CHECK(*applyDelta(s, d) == s);
}

TEST_CASE("added, removed and changed keys land in the right place", "[save][delta]")
{
    const DeltaState base{{"a", bytes({1})}, {"b", bytes({2})}, {"c", bytes({3})}};
    const DeltaState cur{{"a", bytes({1})}, {"b", bytes({9})}, {"d", bytes({4})}};
    const auto d = computeDelta(base, cur);
    CHECK(d.set.size() == 2);
    CHECK(d.set.at("b") == bytes({9}));
    CHECK(d.set.at("d") == bytes({4}));
    CHECK(d.set.count("a") == 0);
    REQUIRE(d.removed.size() == 1);
    CHECK(d.removed[0] == "c");
    CHECK(*applyDelta(base, d) == cur);
}

TEST_CASE("an empty value differs from a missing key", "[save][delta]")
{
    const auto added = computeDelta({}, {{"a", {}}});
    CHECK(added.set.count("a") == 1);
    const auto removed = computeDelta({{"a", {}}}, {});
    CHECK(removed.removed.size() == 1);
}

TEST_CASE("a state equal to the baseline stores nothing however large it is", "[save][delta]")
{
    DeltaState s;
    for (int i = 0; i < 5000; ++i) {
        s["chest/" + std::to_string(i)] = bytes({0, 0, 0, 0});
    }
    DeltaState cur = s;
    cur["chest/77"] = bytes({1, 0, 0, 0});
    const auto enc = encodeDelta(computeDelta(s, cur));
    REQUIRE(enc.has_value());
    CHECK(enc->size() < 40);
}

TEST_CASE("apply rejects a delta that removes a key the baseline lacks", "[save][delta]")
{
    StateDelta d;
    d.removed = {"ghost"};
    CHECK_FALSE(applyDelta({{"a", bytes({1})}}, d).has_value());
}

TEST_CASE("random states round-trip through delta and bytes", "[save][delta]")
{
    std::mt19937 rng(227);
    for (int i = 0; i < 500; ++i) {
        const DeltaState base = randomState(rng, 30);
        const DeltaState cur = randomState(rng, 30);
        const auto d = computeDelta(base, cur);
        const auto enc = encodeDelta(d);
        REQUIRE(enc.has_value());
        const auto dec = decodeDelta(*enc);
        REQUIRE(dec.has_value());
        CHECK(*dec == d);
        CHECK(*encodeDelta(*dec) == *enc);
        const auto applied = applyDelta(base, *dec);
        REQUIRE(applied.has_value());
        CHECK(*applied == cur);
    }
}

TEST_CASE("encoding is independent of removed-key order", "[save][delta]")
{
    StateDelta a;
    a.removed = {"z", "b", "m"};
    StateDelta b;
    b.removed = {"m", "z", "b"};
    CHECK(*encodeDelta(a) == *encodeDelta(b));
}

TEST_CASE("decode rejects unknown versions", "[save][delta]")
{
    CHECK_FALSE(decodeDelta(bytes({0, 0, 0})).has_value());
    CHECK_FALSE(decodeDelta(bytes({2, 0, 0})).has_value());
    CHECK_FALSE(decodeDelta(bytes({255, 0, 0})).has_value());
    CHECK(decodeDelta(bytes({1, 0, 0})).has_value());
    CHECK_FALSE(decodeDelta({}).has_value());
}

TEST_CASE("every truncation of a valid delta is rejected", "[save][delta]")
{
    StateDelta d;
    d.set = {{"alpha", bytes({1, 2, 3})}, {"beta", bytes({})}};
    d.removed = {"gamma", "zeta"};
    const auto enc = *encodeDelta(d);
    for (std::size_t n = 0; n < enc.size(); ++n) {
        CHECK_FALSE(decodeDelta(std::vector<std::uint8_t>(enc.begin(), enc.begin() + static_cast<std::ptrdiff_t>(n))).has_value());
    }
    auto extra = enc;
    extra.push_back(0);
    CHECK_FALSE(decodeDelta(extra).has_value());
}

TEST_CASE("hostile bytes are rejected without crashing", "[save][delta]")
{
    CHECK_FALSE(decodeDelta(bytes({1, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F})).has_value());
    CHECK_FALSE(decodeDelta(bytes({1, 0xFF, 0xFF, 0xFF, 0x7F})).has_value());
    CHECK_FALSE(decodeDelta(bytes({1, 0x80, 0x00, 0})).has_value());
    CHECK_FALSE(decodeDelta(bytes({1, 1, 0, 0})).has_value());
    CHECK_FALSE(decodeDelta(bytes({1, 1, 1, 'a', 0xFF, 0xFF, 0xFF, 0x7F})).has_value());
    CHECK_FALSE(decodeDelta(bytes({1, 0, 1, 0xFF, 0xFF, 0x7F})).has_value());
    CHECK_FALSE(decodeDelta(bytes({1, 2, 1, 'b', 0, 1, 'a', 0, 0})).has_value());
    CHECK_FALSE(decodeDelta(bytes({1, 2, 1, 'a', 0, 1, 'a', 0, 0})).has_value());
    CHECK_FALSE(decodeDelta(bytes({1, 1, 1, 'a', 0, 1, 1, 'a'})).has_value());
    CHECK_FALSE(decodeDelta(bytes({1, 0, 2, 1, 'b', 1, 'a'})).has_value());

    std::mt19937 rng(7);
    for (int i = 0; i < 2000; ++i) {
        std::vector<std::uint8_t> junk(rng() % 64);
        for (auto& b : junk) {
            b = static_cast<std::uint8_t>(rng());
        }
        if (!junk.empty()) {
            junk[0] = 1;
        }
        const auto dec = decodeDelta(junk);
        if (dec) {
            CHECK(*encodeDelta(*dec) == junk);
        }
    }
}

TEST_CASE("size limits hold on encode, decode and apply", "[save][delta]")
{
    using namespace delta_limits;
    StateDelta d;
    d.set["k"] = std::vector<std::uint8_t>(kMaxValueBytes, 7);
    REQUIRE(encodeDelta(d).has_value());
    CHECK(decodeDelta(*encodeDelta(d)).has_value());
    d.set["k"].push_back(0);
    CHECK_FALSE(encodeDelta(d).has_value());
    CHECK_FALSE(applyDelta({}, d).has_value());

    StateDelta keys;
    keys.set[std::string(kMaxKeyBytes, 'x')] = {};
    CHECK(encodeDelta(keys).has_value());
    StateDelta longKey;
    longKey.set[std::string(kMaxKeyBytes + 1, 'x')] = {};
    CHECK_FALSE(encodeDelta(longKey).has_value());
    StateDelta emptyKey;
    emptyKey.set[""] = {};
    CHECK_FALSE(encodeDelta(emptyKey).has_value());

    StateDelta many;
    for (std::size_t i = 0; i <= kMaxEntries; ++i) {
        many.removed.push_back("r" + std::to_string(i));
    }
    CHECK_FALSE(encodeDelta(many).has_value());

    StateDelta both;
    both.set["a"] = {};
    both.removed = {"a"};
    CHECK_FALSE(encodeDelta(both).has_value());
    CHECK_FALSE(applyDelta({{"a", bytes({1})}}, both).has_value());
    CHECK_FALSE(decodeDelta(bytes({1, 1, 1, 'a', 0, 1, 1, 'a'})).has_value());
    StateDelta dup;
    dup.removed = {"a", "a"};
    CHECK_FALSE(encodeDelta(dup).has_value());

    std::vector<std::uint8_t> huge(kMaxEncodedBytes + 1, 0);
    huge[0] = 1;
    CHECK_FALSE(decodeDelta(huge).has_value());
}
