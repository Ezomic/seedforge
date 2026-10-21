#include "ichor/world/forged_water.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <format>
#include <memory>
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

// The sample nearest to the middle of a forged cell, both counted along a side whose ends coincide.
std::int64_t centreSample(std::int64_t cell, std::int64_t span, std::int64_t last)
{
    return std::min((2 * cell * span + last) / (2 * last), span);
}

// A river is a line of samples from the middle of each river cell to the middle of the cell it flows
// into, and not the cell's whole block of samples, so a river meanders as the forge's channel does instead
// of stepping along the cell grid. The engine's own widening then gives it its width. Each sample on a
// line takes the largest catchment of the lines through it, and so does each sample within two of it.
void markRiverLines(const worldforge::HydrologyMap& map, std::int64_t span, std::int64_t last, std::int64_t n, std::vector<std::uint32_t>& accumulation)
{
    const auto at = [&](std::int64_t sx, std::int64_t sz) { return static_cast<std::size_t>(sz * n + sx); };
    for (std::int64_t cz = 0; cz <= last; ++cz) {
        for (std::int64_t cx = 0; cx <= last; ++cx) {
            const std::size_t cell = map.index(static_cast<int>(cx), static_cast<int>(cz));
            if (map.river[cell] == 0) {
                continue;
            }
            const std::uint32_t value = std::max<std::uint32_t>(map.accumulation[cell], 2);
            const std::int64_t x0 = centreSample(cx, span, last);
            const std::int64_t z0 = centreSample(cz, span, last);
            std::int64_t x1 = x0;
            std::int64_t z1 = z0;
            const std::uint8_t dir = map.flowDir[cell];
            if (dir < kDx.size() && cx + kDx[dir] >= 0 && cz + kDz[dir] >= 0 && cx + kDx[dir] <= last && cz + kDz[dir] <= last) {
                x1 = centreSample(cx + kDx[dir], span, last);
                z1 = centreSample(cz + kDz[dir], span, last);
            }
            const std::int64_t steps = std::max<std::int64_t>(std::max(std::abs(x1 - x0), std::abs(z1 - z0)) * 2, 1);
            for (std::int64_t k = 0; k <= steps; ++k) {
                const std::int64_t sx = x0 + (2 * (x1 - x0) * k + (x1 >= x0 ? steps : -steps)) / (2 * steps);
                const std::int64_t sz = z0 + (2 * (z1 - z0) * k + (z1 >= z0 ? steps : -steps)) / (2 * steps);
                std::uint32_t& slot = accumulation[at(sx, sz)];
                slot = std::max(slot, value);
            }
        }
    }
    // A line one sample wide is thinner than the water it should carry on this grid: take two samples either side.
    const std::vector<std::uint32_t> lines = accumulation;
    for (std::int64_t sz = 0; sz < n; ++sz) {
        for (std::int64_t sx = 0; sx < n; ++sx) {
            const std::uint32_t value = lines[at(sx, sz)];
            if (value <= 1) {
                continue;
            }
            for (std::int64_t dz = std::max<std::int64_t>(sz - 2, 0); dz <= std::min(sz + 2, n - 1); ++dz) {
                for (std::int64_t dx = std::max<std::int64_t>(sx - 2, 0); dx <= std::min(sx + 2, n - 1); ++dx) {
                    std::uint32_t& slot = accumulation[at(dx, dz)];
                    slot = std::max(slot, value);
                }
            }
        }
    }
}

int sign(std::int64_t v)
{
    return (v > 0) - (v < 0);
}

// Which of the eight neighbours a sample drains to: the step towards the middle of the cell its own
// cell drains to, kept only when it stays inside the sample's cell or enters that receiver. Within a
// cell every step closes on the receiver's middle, and a step across cells follows the forge's flow,
// which has no cycles, so the flow of samples has none either. Anything else is an outlet.
std::uint8_t sampleDirection(const worldforge::HydrologyMap& map, std::int64_t sx, std::int64_t sz, std::int64_t span, std::int64_t last, std::int64_t n)
{
    const std::int64_t cx = nearestCell(sx, span, last);
    const std::int64_t cz = nearestCell(sz, span, last);
    const std::uint8_t dir = map.flowDir[map.index(static_cast<int>(cx), static_cast<int>(cz))];
    if (dir >= kDx.size()) {
        return Hydrology::kOutlet;
    }
    const std::int64_t rx = cx + kDx[dir];
    const std::int64_t rz = cz + kDz[dir];
    if (rx < 0 || rz < 0 || rx > last || rz > last) {
        return Hydrology::kOutlet;
    }
    const int stepX = sign(centreSample(rx, span, last) - sx);
    const int stepZ = sign(centreSample(rz, span, last) - sz);
    const std::int64_t nx = sx + stepX;
    const std::int64_t nz = sz + stepZ;
    if ((stepX == 0 && stepZ == 0) || nx < 0 || nz < 0 || nx >= n || nz >= n) {
        return Hydrology::kOutlet;
    }
    const std::int64_t ncx = nearestCell(nx, span, last);
    const std::int64_t ncz = nearestCell(nz, span, last);
    const bool inOwn = ncx == cx && ncz == cz;
    const bool inReceiver = ncx == rx && ncz == rz;
    if (!inOwn && !inReceiver) {
        return Hydrology::kOutlet;
    }
    for (std::uint8_t d = 0; d < kDx.size(); ++d) {
        if (kDx[d] == stepX && kDz[d] == stepZ) {
            return d;
        }
    }
    return Hydrology::kOutlet;
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
    for (std::size_t cell = 0; cell < cells; ++cell) {
        if (map.river[cell] != 0) {
            smallestRiver = std::min(smallestRiver, std::max<std::uint32_t>(map.accumulation[cell], 2));
        }
    }
    for (std::int64_t sz = 0; sz < n; ++sz) {
        const std::int64_t cz = nearestCell(sz, span, last);
        for (std::int64_t sx = 0; sx < n; ++sx) {
            const std::size_t cell = static_cast<std::size_t>(cz * map.width + nearestCell(sx, span, last));
            cellOf[static_cast<std::size_t>(sz * n + sx)] = cell;
        }
    }

    auto hydrology = std::make_shared<Hydrology>();
    hydrology->side = static_cast<std::uint32_t>(n);
    hydrology->filled.resize(total);
    hydrology->direction.assign(total, Hydrology::kOutlet);
    hydrology->accumulation.assign(total, 1);
    markRiverLines(map, span, last, n, hydrology->accumulation);
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
            hydrology->direction[i] = sampleDirection(map, sx, sz, span, last, n);
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
    out.rivers.riverThreshold = smallestRiver == UINT32_MAX ? UINT32_MAX : smallestRiver;
    out.rivers.maxLakeDepth = kPlayableLakeDepth;
    return out;
}

}
