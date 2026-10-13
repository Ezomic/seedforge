#include "ichor/world/hydrology.h"

#include <array>
#include <queue>

namespace ichor {

namespace {

constexpr std::array<int, 8> kDx{1, 1, 0, -1, -1, -1, 0, 1};
constexpr std::array<int, 8> kDz{0, 1, 1, 1, 0, -1, -1, -1};

struct Frontier {
    float height = 0.0f;
    std::uint64_t order = 0; // first in, first out among equals
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
    std::uint64_t counter = 0;

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

bool isRiver(const Hydrology& hydrology, std::size_t sample, const HydrologyParams& params)
{
    return sample < hydrology.accumulation.size() && hydrology.accumulation[sample] >= params.riverThreshold;
}

bool isLake(const Hydrology& hydrology, const Heightfield& heightfield, std::uint32_t sx, std::uint32_t sz, const HydrologyParams& params)
{
    if (hydrology.empty() || sx >= hydrology.side || sz >= hydrology.side) {
        return false;
    }
    return hydrology.filled[hydrology.index(sx, sz)] - heightfield.heightAtSample(sx, sz) >= params.lakeDepth;
}

}
