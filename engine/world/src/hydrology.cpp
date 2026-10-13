#include "ichor/world/hydrology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <queue>
#include <vector>

namespace ichor {

namespace {

constexpr std::array<int, 8> kDx{1, 1, 0, -1, -1, -1, 0, 1};
constexpr std::array<int, 8> kDz{0, 1, 1, 1, 0, -1, -1, -1};

struct Frontier {
    float height = 0.0f;
    std::uint32_t order = 0; // first in, first out among equals; below the sample count, so it fits
    std::uint32_t sample = 0;
    bool operator>(const Frontier& other) const
    {
        return height != other.height ? height > other.height : order > other.order;
    }
};

}

std::size_t Hydrology::downstream(std::size_t from) const
{
    const std::uint8_t d = direction[from];
    if (d == kOutlet) {
        return from;
    }
    const auto sx = static_cast<std::int64_t>(from % side) + kDx[d];
    const auto sz = static_cast<std::int64_t>(from / side) + kDz[d];
    return static_cast<std::size_t>(sz) * side + static_cast<std::size_t>(sx);
}

Hydrology computeHydrology(const Heightfield& heightfield)
{
    Hydrology out;
    const std::uint32_t n = heightfield.desc().samplesPerSide();
    if (heightfield.empty() || n < 3) {
        return out;
    }
    const std::size_t total = static_cast<std::size_t>(n) * n;
    out.side = n;
    out.filled.assign(total, 0.0f);
    out.direction.assign(total, Hydrology::kOutlet);
    out.accumulation.assign(total, 1);

    std::vector<bool> reached(total, false);
    std::vector<std::uint32_t> order;
    order.reserve(total);
    std::priority_queue<Frontier, std::vector<Frontier>, std::greater<>> frontier;
    std::uint32_t counter = 0;

    const auto height = [&](std::uint32_t sx, std::uint32_t sz) { return heightfield.heightAtSample(sx, sz); };
    // The flood starts from the map's edge: everything there drains off the map.
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            if (x != 0 && z != 0 && x != n - 1 && z != n - 1) {
                continue;
            }
            const std::size_t i = static_cast<std::size_t>(z) * n + x;
            out.filled[i] = height(x, z);
            reached[i] = true;
            frontier.push({out.filled[i], counter++, static_cast<std::uint32_t>(i)});
        }
    }

    while (!frontier.empty()) {
        const Frontier top = frontier.top();
        frontier.pop();
        order.push_back(top.sample);
        const auto cx = static_cast<std::int64_t>(top.sample % n);
        const auto cz = static_cast<std::int64_t>(top.sample / n);
        for (std::uint8_t d = 0; d < 8; ++d) {
            const std::int64_t nx = cx + kDx[d];
            const std::int64_t nz = cz + kDz[d];
            if (nx < 0 || nz < 0 || nx >= n || nz >= n) {
                continue;
            }
            const std::size_t ni = static_cast<std::size_t>(nz) * n + static_cast<std::size_t>(nx);
            if (reached[ni]) {
                continue;
            }
            reached[ni] = true;
            // A neighbour below the water level of the one that reached it is filled up to it.
            const float ground = height(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(nz));
            out.filled[ni] = ground > top.height ? ground : top.height;
            // It drains back towards the sample that reached it: the opposite direction.
            out.direction[ni] = static_cast<std::uint8_t>((d + 4) % 8);
            frontier.push({out.filled[ni], counter++, static_cast<std::uint32_t>(ni)});
        }
    }

    // Lakes: runs of samples that the flood lifted to one level, joined edge to edge.
    out.lake.assign(total, 0);
    std::vector<std::uint32_t> walk;
    for (std::size_t start = 0; start < total; ++start) {
        const float startGround = height(static_cast<std::uint32_t>(start % n), static_cast<std::uint32_t>(start / n));
        if (out.lake[start] != 0 || !(out.filled[start] > startGround)) {
            continue;
        }
        const auto id = static_cast<std::uint32_t>(out.lakeDeepest.size() + 1);
        const float level = out.filled[start];
        float deepest = 0.0f;
        out.lake[start] = id;
        walk.push_back(static_cast<std::uint32_t>(start));
        while (!walk.empty()) {
            const std::uint32_t at = walk.back();
            walk.pop_back();
            const auto ax = at % n;
            const auto az = at / n;
            deepest = std::max(deepest, level - height(ax, az));
            for (std::uint8_t d = 0; d < 8; d += 2) { // east, south, west, north
                const std::int64_t nx = static_cast<std::int64_t>(ax) + kDx[d];
                const std::int64_t nz = static_cast<std::int64_t>(az) + kDz[d];
                if (nx < 0 || nz < 0 || nx >= n || nz >= n) {
                    continue;
                }
                const std::size_t ni = static_cast<std::size_t>(nz) * n + static_cast<std::size_t>(nx);
                if (out.lake[ni] == 0 && out.filled[ni] == level && out.filled[ni] > height(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(nz))) {
                    out.lake[ni] = id;
                    walk.push_back(static_cast<std::uint32_t>(ni));
                }
            }
        }
        out.lakeDeepest.push_back(deepest);
    }

    // Everything drains to a sample reached before it, so adding up in reverse order of reaching
    // hands each sample's whole catchment to the one below before that one is handed on.
    for (std::size_t k = order.size(); k-- > 0;) {
        const std::size_t sample = order[k];
        if (!out.isOutlet(sample)) {
            out.accumulation[out.downstream(sample)] += out.accumulation[sample];
        }
    }
    return out;
}

namespace {

// How many times a catchment has doubled over the river threshold, counted in whole doublings
// plus a straight line across the next one. Plain float arithmetic, so a forged world is the same
// on every machine.
float doublings(float ratio)
{
    float whole = 0.0f;
    while (ratio >= 2.0f) {
        ratio *= 0.5f;
        whole += 1.0f;
    }
    return whole + (ratio - 1.0f);
}

// A lake sample, or one of its neighbours: the shore and the spill point, which set how deep the lake is.
bool nearLake(const Hydrology& hydrology, const Heightfield& heightfield, std::uint32_t sx, std::uint32_t sz, const HydrologyParams& params)
{
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto nx = static_cast<std::int64_t>(sx) + dx;
            const auto nz = static_cast<std::int64_t>(sz) + dz;
            if (nx >= 0 && nz >= 0 && nx < hydrology.side && nz < hydrology.side
                && isLake(hydrology, heightfield, static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(nz), params)) {
                return true;
            }
        }
    }
    return false;
}

}

Heightfield carveRivers(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, const CarveParams& carve)
{
    const WorldDesc& desc = heightfield.desc();
    if (hydrology.empty() || hydrology.side != desc.samplesPerSide() || heightfield.empty() || rivers.riverThreshold == 0 || !(desc.heightRange > 0.0f)) {
        return heightfield;
    }
    std::vector<std::uint16_t> samples = heightfield.samples();
    const float unitsPerMetre = 65535.0f / desc.heightRange;
    for (std::uint32_t z = 0; z < hydrology.side; ++z) {
        for (std::uint32_t x = 0; x < hydrology.side; ++x) {
            const std::size_t i = hydrology.index(x, z);
            if (!isRiver(hydrology, i, rivers) || hydrology.isOutlet(i) || nearLake(hydrology, heightfield, x, z, rivers)) {
                continue;
            }
            const float ratio = static_cast<float>(hydrology.accumulation[i]) / static_cast<float>(std::max<std::uint32_t>(rivers.riverThreshold, 1));
            const float depth = std::min(carve.baseDepth + carve.depthPerDoubling * doublings(ratio), carve.maxDepth);
            const float lowered = (heightfield.heightAtSample(x, z) - depth - desc.minHeight) * unitsPerMetre;
            samples[i] = static_cast<std::uint16_t>(std::lround(std::clamp(lowered, 0.0f, 65535.0f)));
        }
    }
    return Heightfield(desc, std::move(samples));
}

bool isRiver(const Hydrology& hydrology, std::size_t sample, const HydrologyParams& params)
{
    return sample < hydrology.accumulation.size() && hydrology.accumulation[sample] >= params.riverThreshold;
}

bool isLake(const Hydrology& hydrology, const Heightfield& heightfield, std::uint32_t sx, std::uint32_t sz, const HydrologyParams& params)
{
    if (hydrology.empty() || sx >= hydrology.side || sz >= hydrology.side) {
        return false;
    }
    const std::size_t at = hydrology.index(sx, sz);
    const std::uint32_t id = at < hydrology.lake.size() ? hydrology.lake[at] : 0;
    return id != 0 && hydrology.filled[at] - heightfield.heightAtSample(sx, sz) >= params.lakeDepth && hydrology.lakeDeepest[id - 1] <= params.maxLakeDepth;
}

}
