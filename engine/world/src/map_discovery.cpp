#include "ichor/world/map_discovery.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>

namespace ichor {

namespace {

constexpr std::array<std::uint8_t, 4> kFog{12, 14, 20, 255};
constexpr std::uint32_t kMaxSide = 4096;

std::uint32_t cellOf(float fraction, std::uint32_t side)
{
    const float clamped = std::clamp(fraction, 0.0f, 1.0f);
    return std::min<std::uint32_t>(static_cast<std::uint32_t>(clamped * static_cast<float>(side)), side - 1);
}

constexpr std::array<const char*, 12> kOnsets = {"b", "br", "d", "f", "g", "h", "k", "m", "n", "r", "th", "v"};
constexpr std::array<const char*, 6> kVowels = {"a", "e", "i", "o", "u", "ae"};
constexpr std::array<const char*, 10> kCodas = {"", "n", "r", "l", "s", "th", "m", "nd", "rn", "ck"};

}

MapDiscovery::MapDiscovery(std::uint32_t side)
    : side_(std::clamp<std::uint32_t>(side, 1, kMaxSide))
    , seen_(static_cast<std::size_t>(side_) * side_, 0)
{
}

void MapDiscovery::reveal(MapPoint at, float radius)
{
    if (!std::isfinite(at.u) || !std::isfinite(at.v)) {
        return;
    }
    const std::uint32_t cx = cellOf(at.u, side_);
    const std::uint32_t cy = cellOf(at.v, side_);
    const float cells = std::isfinite(radius) && radius > 0.0f ? std::min(radius, 1.0f) * static_cast<float>(side_) : 0.0f;
    const std::int64_t r = static_cast<std::int64_t>(cells);
    const std::int64_t reach2 = r * r;
    const std::int64_t lo = std::max<std::int64_t>(0, static_cast<std::int64_t>(cy) - r);
    const std::int64_t hi = std::min<std::int64_t>(side_ - 1, static_cast<std::int64_t>(cy) + r);
    for (std::int64_t y = lo; y <= hi; ++y) {
        const std::int64_t dy = y - static_cast<std::int64_t>(cy);
        for (std::int64_t x = std::max<std::int64_t>(0, static_cast<std::int64_t>(cx) - r); x <= std::min<std::int64_t>(side_ - 1, static_cast<std::int64_t>(cx) + r); ++x) {
            const std::int64_t dx = x - static_cast<std::int64_t>(cx);
            std::uint8_t& cell = seen_[static_cast<std::size_t>(y) * side_ + static_cast<std::size_t>(x)];
            if (dx * dx + dy * dy <= reach2 && cell == 0) {
                cell = 1;
                ++seenCount_;
            }
        }
    }
}

void MapDiscovery::revealSegment(MapPoint from, MapPoint to, float radius)
{
    if (!std::isfinite(from.u) || !std::isfinite(from.v) || !std::isfinite(to.u) || !std::isfinite(to.v)) {
        return;
    }
    const float length = std::min(std::max(std::fabs(to.u - from.u), std::fabs(to.v - from.v)), 1.0f) * static_cast<float>(side_);
    const int steps = static_cast<int>(length) * 2 + 1;
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        reveal({from.u + (to.u - from.u) * t, from.v + (to.v - from.v) * t}, radius);
    }
}

bool MapDiscovery::seen(MapPoint at) const
{
    if (!std::isfinite(at.u) || !std::isfinite(at.v)) {
        return false;
    }
    return seenCell(cellOf(at.u, side_), cellOf(at.v, side_));
}

bool MapDiscovery::seenCell(std::uint32_t x, std::uint32_t y) const
{
    return x < side_ && y < side_ && seen_[static_cast<std::size_t>(y) * side_ + x] != 0;
}

MapImage withFog(const MapImage& image, const MapDiscovery& discovery)
{
    if (image.empty() || image.width != discovery.side() || image.height != discovery.side()) {
        return image;
    }
    MapImage out = image;
    for (std::uint32_t y = 0; y < out.height; ++y) {
        for (std::uint32_t x = 0; x < out.width; ++x) {
            if (!discovery.seenCell(x, y)) {
                std::copy(kFog.begin(), kFog.end(), out.rgba.begin() + (static_cast<std::size_t>(y) * out.width + x) * 4);
            }
        }
    }
    return out;
}

std::string settlementMapName(std::uint64_t worldSeed, std::string_view settlementId)
{
    std::uint64_t h = mix64(hash64(settlementId, worldSeed ^ 0x4d4150ull));
    const auto pick = [&](std::size_t count) {
        h = mix64(h);
        return static_cast<std::size_t>(h % count);
    };
    std::string name;
    const std::size_t syllables = 2 + pick(2);
    for (std::size_t i = 0; i < syllables; ++i) {
        name += kOnsets[pick(kOnsets.size())];
        name += kVowels[pick(kVowels.size())];
    }
    name += kCodas[pick(kCodas.size())];
    name[0] = static_cast<char>(name[0] - 'a' + 'A');
    return name;
}

std::vector<MapSettlement> mapSettlements(const WorldDesc& desc, std::uint64_t worldSeed, const std::vector<SettlementSite>& sites)
{
    std::vector<MapSettlement> out;
    std::set<std::string> used;
    for (const SettlementSite& site : sites) {
        std::string name = settlementMapName(worldSeed, site.id);
        for (std::uint64_t salt = 1; used.contains(name) && salt < 64; ++salt) {
            name = settlementMapName(worldSeed + salt, site.id);
        }
        for (std::uint64_t n = 2; used.contains(name); ++n) {
            name = settlementMapName(worldSeed, site.id) + " " + std::to_string(n);
        }
        used.insert(name);
        out.push_back({site.id, std::move(name), std::min<std::uint8_t>(site.tier, 3), mapPosition(desc, site.x, site.z)});
    }
    return out;
}

}
