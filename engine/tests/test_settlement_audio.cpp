#include "ichor/audio/settlement_audio.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <string>
#include <tuple>

using namespace ichor;

namespace {

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

}  // namespace

TEST_CASE("a settlement sounds the same every time", "[audio][settlement]")
{
    const auto a = settlementSignature("riverton", 0.5f, SettlementSoundType::Milling);
    const auto b = settlementSignature("riverton", 0.5f, SettlementSoundType::Milling);
    CHECK(a == b);
}

TEST_CASE("the type decides the dominant sound", "[audio][settlement]")
{
    CHECK(settlementSignature("x", 0.5f, SettlementSoundType::Sacred).dominant == SettlementSound::Bell);
    CHECK(settlementSignature("x", 0.5f, SettlementSoundType::Milling).dominant == SettlementSound::Mill);
    CHECK(settlementSignature("x", 0.5f, SettlementSoundType::Coastal).dominant == SettlementSound::Surf);
    CHECK(settlementSignature("x", 0.5f, SettlementSoundType::Mining).dominant == SettlementSound::Machinery);
}

TEST_CASE("signature parameters stay in range", "[audio][settlement]")
{
    for (int i = 0; i < 500; ++i) {
        const auto s = settlementSignature("town" + std::to_string(i), 0.3f, SettlementSoundType::Sacred);
        CHECK(s.pitchBand < kSettlementPitchBands);
        CHECK(s.rhythmBpm >= kSettlementMinBpm);
        CHECK(s.rhythmBpm <= kSettlementMaxBpm);
    }
}

// The metric is the share of settlements whose full signature is already taken by an earlier one.
// If the id were ignored every settlement of a type would collide, so this goes red at ~100%.
TEST_CASE("settlements of one type are told apart by their id", "[audio][settlement]")
{
    constexpr int kCount = 2000;
    std::set<std::tuple<std::uint8_t, std::uint16_t, std::uint16_t>> seen;
    int collisions = 0;
    for (int i = 0; i < kCount; ++i) {
        const auto s = settlementSignature("settlement-" + std::to_string(i), 0.5f, SettlementSoundType::Sacred);
        if (!seen.emplace(s.pitchBand, s.rhythmBpm, s.pattern).second) {
            ++collisions;
        }
    }
    CHECK(collisions < kCount / 100);
}

TEST_CASE("a near neighbour is not a near twin", "[audio][settlement]")
{
    const auto a = settlementSignature("Ashford", 0.5f, SettlementSoundType::Coastal);
    const auto b = settlementSignature("Ashfore", 0.5f, SettlementSoundType::Coastal);
    CHECK_FALSE(a == b);
}

TEST_CASE("size lowers the pitch without changing the identity", "[audio][settlement]")
{
    const auto small = settlementSignature("same", 0.0f, SettlementSoundType::Sacred);
    const auto large = settlementSignature("same", 1.0f, SettlementSoundType::Sacred);
    CHECK(small.pattern == large.pattern);
    CHECK(small.rhythmBpm == large.rhythmBpm);
    CHECK(large.pitchBand <= small.pitchBand);
}

TEST_CASE("hostile ids still give a valid signature", "[audio][settlement]")
{
    const std::string big(100000, 'z');
    const std::string nul("a\0b", 3);
    for (const auto* id : {"", " ", "\xff\xfe"}) {
        const auto s = settlementSignature(id, 0.5f, SettlementSoundType::Mining);
        CHECK(s.pitchBand < kSettlementPitchBands);
    }
    CHECK(settlementSignature(big, 0.5f, SettlementSoundType::Mining).pitchBand < kSettlementPitchBands);
    CHECK_FALSE(settlementSignature(nul, 0.5f, SettlementSoundType::Mining) ==
                settlementSignature("a", 0.5f, SettlementSoundType::Mining));
}

TEST_CASE("an unknown type falls back to an inland sound", "[audio][settlement]")
{
    const auto s = settlementSignature("x", 0.5f, static_cast<SettlementSoundType>(200));
    CHECK(s.dominant == settlementSignature("x", 0.5f, SettlementSoundType::Inland).dominant);
}

TEST_CASE("NaN and extreme sizes are clamped", "[audio][settlement]")
{
    const auto lo = settlementSignature("q", 0.0f, SettlementSoundType::Sacred);
    const auto hi = settlementSignature("q", 1.0f, SettlementSoundType::Sacred);
    CHECK(settlementSignature("q", kNan, SettlementSoundType::Sacred) == lo);
    CHECK(settlementSignature("q", -5.0f, SettlementSoundType::Sacred) == lo);
    CHECK(settlementSignature("q", -kInf, SettlementSoundType::Sacred) == lo);
    CHECK(settlementSignature("q", 1e30f, SettlementSoundType::Sacred) == hi);
    CHECK(settlementSignature("q", kInf, SettlementSoundType::Sacred) == hi);
}

TEST_CASE("bigger settlements carry further, within bounds", "[audio][settlement]")
{
    const float hamlet = settlementAudibleRange(0.0f, 0.0f, 0.0f);
    const float city = settlementAudibleRange(1.0f, 0.0f, 0.0f);
    CHECK(hamlet == kSettlementMinRangeM);
    CHECK(city == kSettlementMaxRangeM);
    CHECK(settlementAudibleRange(0.5f, 0.0f, 0.0f) > hamlet);
    CHECK(settlementAudibleRange(0.5f, 0.0f, 0.0f) < city);
}

TEST_CASE("wind carries sound downwind and shortens it upwind", "[audio][settlement]")
{
    const float calm = settlementAudibleRange(0.5f, 0.0f, 0.0f);
    const float down = settlementAudibleRange(0.5f, 15.0f, 1.0f);
    const float up = settlementAudibleRange(0.5f, 15.0f, -1.0f);
    CHECK(down > calm);
    CHECK(up < calm);
    CHECK(up > 0.0f);
    CHECK(settlementAudibleRange(0.5f, 15.0f, 0.0f) == calm);
}

TEST_CASE("range is safe for NaN and extreme inputs", "[audio][settlement]")
{
    for (const float size : {kNan, -kInf, kInf, 1e30f, -1.0f}) {
        for (const float wind : {kNan, -kInf, kInf, 1e30f, -3.0f}) {
            for (const float along : {kNan, -kInf, kInf, 7.0f}) {
                const float r = settlementAudibleRange(size, wind, along);
                CHECK(std::isfinite(r));
                CHECK(r > 0.0f);
                CHECK(r <= kSettlementMaxRangeM * 1.5f);
            }
        }
    }
}

TEST_CASE("gain falls smoothly to exactly zero at the range", "[audio][settlement]")
{
    CHECK(settlementGainAt(0.0f, 500.0f) == 1.0f);
    CHECK(settlementGainAt(500.0f, 500.0f) == 0.0f);
    CHECK(settlementGainAt(900.0f, 500.0f) == 0.0f);
    float prev = 1.0f;
    for (int d = 0; d <= 500; d += 25) {
        const float g = settlementGainAt(static_cast<float>(d), 500.0f);
        CHECK(g <= prev);
        prev = g;
    }
    CHECK(settlementGainAt(250.0f, 500.0f) > 0.0f);
}

TEST_CASE("gain is silent for bad distances and ranges", "[audio][settlement]")
{
    CHECK(settlementGainAt(kNan, 500.0f) == 0.0f);
    CHECK(settlementGainAt(10.0f, kNan) == 0.0f);
    CHECK(settlementGainAt(10.0f, 0.0f) == 0.0f);
    CHECK(settlementGainAt(10.0f, -5.0f) == 0.0f);
    CHECK(settlementGainAt(kInf, 500.0f) == 0.0f);
    CHECK(settlementGainAt(-10.0f, 500.0f) == 1.0f);
    CHECK(settlementGainAt(10.0f, kInf) == 0.0f);
}
