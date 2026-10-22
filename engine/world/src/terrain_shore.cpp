#include "ichor/world/terrain_shore.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ichor {

namespace {

constexpr float kFar = 1.0e9f;
constexpr float kReachMetres = 12.0f;
constexpr float kSandNear = 3.0f;
constexpr float kSandFar = 9.0f;
constexpr float kSandLow = 1.0f;
constexpr float kSandHigh = 3.0f;
constexpr float kWetNear = 1.0f;
constexpr float kWetFar = 6.0f;

float smooth(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Grid {
    int width = 0;
    int height = 0;
    std::vector<float> still; // metres to the nearest lake or sea sample
    std::vector<float> level; // surface height of that water
    std::vector<float> river; // metres to the nearest river bank, negative inside the channel
};

struct Step {
    int dx;
    int dz;
    float cost;
};

void relax(Grid& g, std::size_t at, int x, int z, const Step& step, float spacing)
{
    const int nx = x + step.dx;
    const int nz = z + step.dz;
    if (nx < 0 || nz < 0 || nx >= g.width || nz >= g.height) return;
    const auto from = static_cast<std::size_t>(nz) * static_cast<std::size_t>(g.width) + static_cast<std::size_t>(nx);
    const float cost = step.cost * spacing;
    if (g.still[from] + cost < g.still[at]) {
        g.still[at] = g.still[from] + cost;
        g.level[at] = g.level[from];
    }
    g.river[at] = std::min(g.river[at], g.river[from] + cost);
}

void sweep(Grid& g, float spacing)
{
    constexpr float kDiagonal = 1.41421356f;
    constexpr std::array<Step, 4> forward{{{-1, 0, 1.0f}, {-1, -1, kDiagonal}, {0, -1, 1.0f}, {1, -1, kDiagonal}}};
    constexpr std::array<Step, 4> backward{{{1, 0, 1.0f}, {1, 1, kDiagonal}, {0, 1, 1.0f}, {-1, 1, kDiagonal}}};
    for (int z = 0; z < g.height; ++z) {
        for (int x = 0; x < g.width; ++x) {
            const auto at = static_cast<std::size_t>(z) * static_cast<std::size_t>(g.width) + static_cast<std::size_t>(x);
            for (const Step& step : forward) relax(g, at, x, z, step, spacing);
        }
    }
    for (int z = g.height - 1; z >= 0; --z) {
        for (int x = g.width - 1; x >= 0; --x) {
            const auto at = static_cast<std::size_t>(z) * static_cast<std::size_t>(g.width) + static_cast<std::size_t>(x);
            for (const Step& step : backward) relax(g, at, x, z, step, spacing);
        }
    }
}

}

std::vector<glm::vec2> terrainShore(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, CellId id)
{
    const WorldDesc& desc = heightfield.desc();
    if (hydrology.empty() || hydrology.side != desc.samplesPerSide() || !heightfield.contains(id)) {
        return {};
    }
    const std::uint32_t cellSide = desc.samplesPerCellSide();
    const auto spacing = static_cast<float>(desc.metresPerSample);
    const auto margin = static_cast<std::int64_t>(std::ceil(kReachMetres / spacing));
    const auto worldSide = static_cast<std::int64_t>(desc.samplesPerSide());
    const std::int64_t firstX = static_cast<std::int64_t>(id.x) * static_cast<std::int64_t>(cellSide - 1);
    const std::int64_t firstZ = static_cast<std::int64_t>(id.z) * static_cast<std::int64_t>(cellSide - 1);
    const std::int64_t x0 = std::max<std::int64_t>(firstX - margin, 0);
    const std::int64_t z0 = std::max<std::int64_t>(firstZ - margin, 0);
    const std::int64_t x1 = std::min<std::int64_t>(firstX + cellSide - 1 + margin, worldSide - 1);
    const std::int64_t z1 = std::min<std::int64_t>(firstZ + cellSide - 1 + margin, worldSide - 1);

    Grid g;
    g.width = static_cast<int>(x1 - x0 + 1);
    g.height = static_cast<int>(z1 - z0 + 1);
    const auto total = static_cast<std::size_t>(g.width) * static_cast<std::size_t>(g.height);
    g.still.assign(total, kFar);
    g.level.assign(total, 0.0f);
    g.river.assign(total, kFar);
    std::vector<float> ground(total, 0.0f);

    const float sea = desc.hasWater() ? desc.waterLevel() : -kFar;
    for (int z = 0; z < g.height; ++z) {
        for (int x = 0; x < g.width; ++x) {
            const auto sx = static_cast<std::uint32_t>(x0 + x);
            const auto sz = static_cast<std::uint32_t>(z0 + z);
            const auto at = static_cast<std::size_t>(z) * static_cast<std::size_t>(g.width) + static_cast<std::size_t>(x);
            const std::size_t sample = hydrology.index(sx, sz);
            const float h = heightfield.heightAtSample(sx, sz);
            ground[at] = h;
            if (isLake(hydrology, heightfield, sx, sz, rivers)) {
                g.still[at] = 0.0f;
                g.level[at] = hydrology.filled[sample];
            } else if (h < sea) {
                g.still[at] = 0.0f;
                g.level[at] = sea;
            } else if (isRiver(hydrology, sample, rivers)) {
                g.river[at] = -riverHalfWidth(hydrology.accumulation[sample], rivers);
            }
        }
    }
    sweep(g, spacing);

    std::vector<glm::vec2> out;
    out.reserve(static_cast<std::size_t>(cellSide) * cellSide);
    for (std::uint32_t lz = 0; lz < cellSide; ++lz) {
        for (std::uint32_t lx = 0; lx < cellSide; ++lx) {
            const auto x = static_cast<int>(firstX + lx - x0);
            const auto z = static_cast<int>(firstZ + lz - z0);
            const auto at = static_cast<std::size_t>(z) * static_cast<std::size_t>(g.width) + static_cast<std::size_t>(x);
            const float above = ground[at] - g.level[at];
            const float sand = g.still[at] >= kFar ? 0.0f : (1.0f - smooth(kSandNear, kSandFar, g.still[at])) * (1.0f - smooth(kSandLow, kSandHigh, above));
            const float wet = g.river[at] >= kFar ? 0.0f : 1.0f - smooth(kWetNear, kWetFar, g.river[at]);
            out.emplace_back(sand, wet);
        }
    }
    return out;
}

}
