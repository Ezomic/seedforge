#include "ichor/world/forged_water.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <utility>
#include <vector>

namespace ichor {

namespace {

constexpr std::array<int, 8> kDx{1, 1, 0, -1, -1, -1, 0, 1};
constexpr std::array<int, 8> kDz{0, 1, 1, 1, 0, -1, -1, -1};

ForgedWater failure(std::string message)
{
    ForgedWater out;
    out.error = std::move(message);
    return out;
}

// The forged cell nearest to sample s, both counted along a side whose ends coincide.
std::int64_t nearestCell(std::int64_t s, std::int64_t span, std::int64_t last)
{
    return std::min((2 * s * last + span) / (2 * span), last);
}

}

ForgedWater waterFromForgedWorld(const worldforge::ForgedWorld& world, const Heightfield& heightfield)
{
    const std::int64_t n = heightfield.desc().samplesPerSide();
    if (heightfield.empty() || n < 3) {
        return failure("the heightfield is empty or smaller than 3 samples a side");
    }
    if (!world.hydrology().has_value()) {
        return failure(world.ok() ? "the forged world stopped before hydrology" : world.error());
    }
    const worldforge::HydrologyMap& map = *world.hydrology();
    const std::size_t cells = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
    if (map.width < 2 || map.width != map.height || map.filledM.size() != cells || map.lakeDepthM.size() != cells || map.flowDir.size() != cells
        || map.accumulation.size() != cells || map.river.size() != cells) {
        return failure(std::format("the forged hydrology is {}x{} and not a complete square grid", map.width, map.height));
    }

    const std::int64_t span = n - 1;
    const std::int64_t last = map.width - 1;
    const std::size_t total = static_cast<std::size_t>(n * n);

    std::vector<std::size_t> cellOf(total);
    std::uint32_t smallestRiver = UINT32_MAX;
    for (std::int64_t sz = 0; sz < n; ++sz) {
        const std::int64_t cz = nearestCell(sz, span, last);
        for (std::int64_t sx = 0; sx < n; ++sx) {
            const std::size_t cell = static_cast<std::size_t>(cz * map.width + nearestCell(sx, span, last));
            cellOf[static_cast<std::size_t>(sz * n + sx)] = cell;
            if (map.river[cell] != 0) {
                smallestRiver = std::min(smallestRiver, map.accumulation[cell]);
            }
        }
    }

    auto hydrology = std::make_shared<Hydrology>();
    hydrology->side = static_cast<std::uint32_t>(n);
    hydrology->filled.resize(total);
    hydrology->direction.assign(total, Hydrology::kOutlet);
    hydrology->accumulation.assign(total, 0);
    hydrology->lake.assign(total, 0);

    std::vector<bool> lakeSample(total, false);
    for (std::int64_t sz = 0; sz < n; ++sz) {
        for (std::int64_t sx = 0; sx < n; ++sx) {
            const std::size_t i = static_cast<std::size_t>(sz * n + sx);
            const std::size_t cell = cellOf[i];
            const float ground = heightfield.heightAtSample(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sz));
            hydrology->filled[i] = ground;
            if (map.lakeDepthM[cell] != 0) {
                hydrology->filled[i] = std::max(ground, static_cast<float>(map.filledM[cell]));
                lakeSample[i] = hydrology->filled[i] > ground;
            }
            if (map.river[cell] != 0) {
                hydrology->accumulation[i] = map.accumulation[cell];
            }
            const std::uint8_t dir = map.flowDir[cell];
            if (dir < kDx.size() && sx + kDx[dir] >= 0 && sx + kDx[dir] < n && sz + kDz[dir] >= 0 && sz + kDz[dir] < n) {
                hydrology->direction[i] = dir;
            }
        }
    }

    std::vector<std::size_t> walk;
    for (std::size_t start = 0; start < total; ++start) {
        if (!lakeSample[start] || hydrology->lake[start] != 0) {
            continue;
        }
        const auto id = static_cast<std::uint32_t>(hydrology->lakeDeepest.size() + 1);
        const float level = hydrology->filled[start];
        float deepest = 0.0f;
        hydrology->lake[start] = id;
        walk.push_back(start);
        while (!walk.empty()) {
            const std::size_t at = walk.back();
            walk.pop_back();
            const auto ax = static_cast<std::int64_t>(at % static_cast<std::size_t>(n));
            const auto az = static_cast<std::int64_t>(at / static_cast<std::size_t>(n));
            deepest = std::max(deepest, level - heightfield.heightAtSample(static_cast<std::uint32_t>(ax), static_cast<std::uint32_t>(az)));
            for (std::size_t d = 0; d < 8; d += 2) {
                const std::int64_t nx = ax + kDx[d];
                const std::int64_t nz = az + kDz[d];
                if (nx < 0 || nz < 0 || nx >= n || nz >= n) {
                    continue;
                }
                const std::size_t ni = static_cast<std::size_t>(nz * n + nx);
                if (lakeSample[ni] && hydrology->lake[ni] == 0 && hydrology->filled[ni] == level) {
                    hydrology->lake[ni] = id;
                    walk.push_back(ni);
                }
            }
        }
        hydrology->lakeDeepest.push_back(deepest);
    }

    ForgedWater out;
    out.hydrology = std::move(hydrology);
    out.rivers.riverThreshold = smallestRiver == UINT32_MAX ? 1 : smallestRiver;
    out.rivers.maxLakeDepth = kPlayableLakeDepth;
    return out;
}

}
