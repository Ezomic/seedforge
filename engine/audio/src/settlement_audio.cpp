#include "ichor/audio/settlement_audio.h"

namespace ichor {

namespace {

// FNV-1a finalised with SplitMix64, because FNV alone leaves its low bits weak.
constexpr std::uint64_t hashId(std::string_view text)
{
    std::uint64_t h = 14695981039346656037ull;
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    std::uint64_t z = h + 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

constexpr float clamp01(float v)
{
    if (!(v > 0.0f)) {
        return 0.0f;
    }
    return v > 1.0f ? 1.0f : v;
}

constexpr float clampSigned(float v)
{
    if (v != v) {
        return 0.0f;
    }
    if (v < -1.0f) {
        return -1.0f;
    }
    return v > 1.0f ? 1.0f : v;
}

constexpr SettlementSound dominantFor(SettlementSoundType type)
{
    switch (type) {
    case SettlementSoundType::Sacred: return SettlementSound::Bell;
    case SettlementSoundType::Milling: return SettlementSound::Mill;
    case SettlementSoundType::Coastal: return SettlementSound::Surf;
    case SettlementSoundType::Mining: return SettlementSound::Machinery;
    case SettlementSoundType::Inland: break;
    }
    return SettlementSound::Chatter;
}

}  // namespace

SettlementSignature settlementSignature(std::string_view id, float size, SettlementSoundType type)
{
    const std::uint64_t h = hashId(id);
    const float tier = clamp01(size);

    SettlementSignature s;
    s.dominant = dominantFor(type);
    s.pattern = static_cast<std::uint16_t>(h & 0xffffu);
    s.rhythmBpm = static_cast<std::uint16_t>(
        kSettlementMinBpm + (h >> 16) % (kSettlementMaxBpm - kSettlementMinBpm + 1));
    const auto base = static_cast<std::uint8_t>((h >> 40) % kSettlementPitchBands);
    const auto drop = static_cast<std::uint8_t>(tier * static_cast<float>(kSettlementPitchBands / 2));
    s.pitchBand = base > drop ? static_cast<std::uint8_t>(base - drop) : std::uint8_t{0};
    return s;
}

float settlementAudibleRange(float size, float windSpeed, float windAlongPath)
{
    const float base = kSettlementMinRangeM + clamp01(size) * (kSettlementMaxRangeM - kSettlementMinRangeM);
    const float speed = clamp01(windSpeed / kSettlementMaxWindSpeed);
    return base * (1.0f + kSettlementWindRangeShare * speed * clampSigned(windAlongPath));
}

float settlementGainAt(float distance, float range)
{
    if (!(range > 0.0f) || !(range < 1e30f) || !(distance < range)) {
        return 0.0f;
    }
    if (!(distance > 0.0f)) {
        return 1.0f;
    }
    const float t = 1.0f - distance / range;
    return t * t;
}

}  // namespace ichor
