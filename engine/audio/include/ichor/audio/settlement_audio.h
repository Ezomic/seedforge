#pragma once

#include <cstdint>
#include <string_view>

namespace ichor {

// The audible landmark of docs/12-audio-atmosphere.md: every settlement gets a signature with a
// long falloff, so sound still navigates when visibility fails. Pure functions of their inputs,
// so they hold no sim state, never feed the world hash and sound the same on every platform
// (integer hashing, no libm).
enum class SettlementSoundType : std::uint8_t { Inland, Sacred, Milling, Coastal, Mining };

enum class SettlementSound : std::uint8_t { Chatter, Bell, Mill, Surf, Machinery };

constexpr std::uint8_t kSettlementPitchBands = 8;
constexpr std::uint16_t kSettlementMinBpm = 24;
constexpr std::uint16_t kSettlementMaxBpm = 96;
constexpr float kSettlementMinRangeM = 200.0f;
constexpr float kSettlementMaxRangeM = 1200.0f;
constexpr float kSettlementMaxWindSpeed = 20.0f;
constexpr float kSettlementWindRangeShare = 0.4f;

struct SettlementSignature {
    SettlementSound dominant = SettlementSound::Chatter;
    // 0 is the lowest. Larger settlements sound lower.
    std::uint8_t pitchBand = 0;
    std::uint16_t rhythmBpm = kSettlementMinBpm;
    // 16 steps of the rhythm, one bit each, set when the sound strikes.
    std::uint16_t pattern = 0;

    friend bool operator==(const SettlementSignature&, const SettlementSignature&) = default;
};

// The type picks the dominant sound (an unknown type counts as Inland); the id picks the rest, so
// two mills sound like mills but not like each other. size is a normalised 0..1 tier; NaN counts
// as 0 and anything else outside the range is clamped. Any id is valid, empty included.
SettlementSignature settlementSignature(std::string_view id, float size, SettlementSoundType type);

// How far the sound carries, in metres. windSpeed is in m/s (NaN or negative counts as calm,
// capped at kSettlementMaxWindSpeed); windAlongPath is -1 for a listener directly upwind of the
// settlement to +1 directly downwind (NaN counts as 0, clamped). Always finite and positive.
float settlementAudibleRange(float size, float windSpeed, float windAlongPath);

// Gain 0..1 at a distance: 1 at the settlement, easing to exactly 0 at the range and beyond.
// NaN, an unusable range or an infinite distance are silent; a negative distance counts as 0.
float settlementGainAt(float distance, float range);

}  // namespace ichor
