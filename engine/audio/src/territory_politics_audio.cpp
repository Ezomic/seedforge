#include "ichor/audio/territory_politics_audio.h"

#include <vector>

namespace ichor {

namespace {

bool finite(float v)
{
    return v - v == 0.0f;
}

float clampRange(float v, float lo, float hi)
{
    if (!(v > lo)) {
        return lo;
    }
    return v > hi ? hi : v;
}

bool paramsValid(const TerritoryPoliticsParams& p)
{
    return p.bleedCells >= 0 && p.bleedCells <= kTerritoryAudioMaxBleed && p.ownerStrength <= kTerritoryAudioMaxStrength
        && p.contestedOwnerStrength <= kTerritoryAudioMaxStrength && p.contestedRivalStrength <= kTerritoryAudioMaxStrength;
}

void bleedInto(std::vector<std::uint16_t>& out, const std::vector<std::uint16_t>& base, std::uint32_t width, std::uint32_t height, int radius)
{
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    out = base;
    if (radius == 0) {
        return;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint32_t s = base[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
            if (s == 0) {
                continue;
            }
            for (int dy = -radius; dy <= radius; ++dy) {
                const int ny = y + dy;
                if (ny < 0 || ny >= h) {
                    continue;
                }
                const int rest = radius - (dy < 0 ? -dy : dy);
                for (int dx = -rest; dx <= rest; ++dx) {
                    const int nx = x + dx;
                    if (nx < 0 || nx >= w) {
                        continue;
                    }
                    const int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                    const std::uint32_t v = s * static_cast<std::uint32_t>(radius + 1 - d) / static_cast<std::uint32_t>(radius + 1);
                    std::uint16_t& slot = out[static_cast<std::size_t>(ny) * width + static_cast<std::size_t>(nx)];
                    if (v > slot) {
                        slot = static_cast<std::uint16_t>(v);
                    }
                }
            }
        }
    }
}

}  // namespace

std::optional<TerritoryAudioGrid> territoryGridFromPolitics(std::span<const std::uint8_t> owner, std::span<const std::uint8_t> rival,
    std::uint32_t width, std::uint32_t height, int factionCount, float cellSize, float originX, float originZ, const TerritoryPoliticsParams& params)
{
    if (width == 0 || height == 0 || factionCount < 1 || static_cast<std::size_t>(factionCount) > kTerritoryAudioMaxFactions) {
        return std::nullopt;
    }
    const std::uint64_t cells = static_cast<std::uint64_t>(width) * height;
    if (cells > kTerritoryAudioMaxCells || owner.size() != cells || rival.size() != cells) {
        return std::nullopt;
    }
    if (!finite(cellSize) || !(cellSize > 0.0f) || !finite(originX) || !finite(originZ) || !paramsValid(params)) {
        return std::nullopt;
    }

    const std::size_t n = static_cast<std::size_t>(cells);
    const std::size_t count = static_cast<std::size_t>(factionCount);
    std::vector<std::vector<std::uint16_t>> base(count, std::vector<std::uint16_t>(n, 0));
    for (std::size_t c = 0; c < n; ++c) {
        const std::uint8_t o = owner[c];
        const std::uint8_t r = rival[c];
        if (o == kTerritoryAudioNoOwner) {
            if (r != kTerritoryAudioNoOwner) {
                return std::nullopt;
            }
            continue;
        }
        if (o >= count) {
            return std::nullopt;
        }
        if (r == kTerritoryAudioNoOwner) {
            base[o][c] = params.ownerStrength;
            continue;
        }
        if (r >= count || r == o) {
            return std::nullopt;
        }
        base[o][c] = params.contestedOwnerStrength;
        base[r][c] = params.contestedRivalStrength;
    }

    TerritoryAudioGrid grid;
    grid.width = width;
    grid.height = height;
    grid.cellSize = cellSize;
    grid.originX = originX;
    grid.originZ = originZ;
    grid.strength.resize(count);
    for (std::size_t f = 0; f < count; ++f) {
        bleedInto(grid.strength[f], base[f], width, height, params.bleedCells);
    }
    return grid;
}

TerritoryTrackResult territoryTrack(TerritoryTracker& tracker, const TerritoryAudioMix& mix, const TerritoryHysteresis& h)
{
    const float margin = clampRange(h.switchMargin, 0.0f, 1.0f);
    const float release = clampRange(h.releaseBelow, 0.0f, 1.0f);
    const float acquire = clampRange(h.acquireAbove, release, 1.0f);

    const auto gainOf = [&](int i) {
        if (i < 0 || static_cast<std::size_t>(i) >= mix.gains.size()) {
            return 0.0f;
        }
        const float g = mix.gains[static_cast<std::size_t>(i)];
        return finite(g) ? clampRange(g, 0.0f, 1.0f) : 0.0f;
    };

    int best = -1;
    float bestGain = 0.0f;
    for (std::size_t i = 0; i < mix.gains.size(); ++i) {
        const float g = gainOf(static_cast<int>(i));
        if (g > bestGain) {
            bestGain = g;
            best = static_cast<int>(i);
        }
    }

    const int before = (tracker.held >= 0 && static_cast<std::size_t>(tracker.held) < mix.gains.size()) ? tracker.held : -1;
    int next = before;
    if (before < 0) {
        if (best >= 0 && bestGain >= acquire) {
            next = best;
        }
    } else if (gainOf(before) < release) {
        next = (best >= 0 && bestGain >= acquire) ? best : -1;
    } else if (best != before && bestGain > gainOf(before) + margin) {
        next = best;
    }

    tracker.held = next;
    TerritoryTrackResult result;
    result.dominant = next;
    result.changed = next != before;
    return result;
}

}  // namespace ichor
