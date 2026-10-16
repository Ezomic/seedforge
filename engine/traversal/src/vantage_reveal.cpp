#include "ichor/traversal/vantage_reveal.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace ichor {

namespace {

constexpr float kNegInf = -std::numeric_limits<float>::infinity();

bool sample(const VantageHeightFn& height, std::int64_t x, std::int64_t y, float& out)
{
    const float h = height(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
    if (!std::isfinite(h)) {
        return false;
    }
    out = std::clamp(h, -kVantageMaxAbsHeight, kVantageMaxAbsHeight);
    return true;
}

float slopeTo(float eye, float terrain, std::int64_t dx, std::int64_t dy)
{
    return (terrain - eye) / std::sqrt(static_cast<float>(dx * dx + dy * dy));
}

std::int64_t roundedStep(std::int64_t delta, std::int64_t i, std::int64_t steps)
{
    const std::int64_t half = steps / 2;
    return (delta * i + (delta >= 0 ? half : -half)) / steps;
}

}

VantageReveal computeVantageReveal(const VantagePoint& vantage, const VantageHeightFn& height, std::size_t sampleBudget)
{
    VantageReveal result;
    if (!height || !std::isfinite(vantage.eyeHeight) || !std::isfinite(vantage.radiusCells) || vantage.radiusCells < 0.0f) {
        result.status = VantageStatus::InvalidInput;
        return result;
    }
    const float eye = std::clamp(vantage.eyeHeight, -kVantageMaxAbsHeight, kVantageMaxAbsHeight);
    const std::int64_t radius = static_cast<std::int64_t>(std::floor(std::min(vantage.radiusCells, kVantageMaxRadiusCells)));
    const std::int64_t r2 = radius * radius;
    const std::int64_t cx = vantage.cell.x;
    const std::int64_t cy = vantage.cell.y;
    const std::int64_t minX = std::max<std::int64_t>(cx - radius, std::numeric_limits<std::int32_t>::min());
    const std::int64_t maxX = std::min<std::int64_t>(cx + radius, std::numeric_limits<std::int32_t>::max());
    const std::int64_t minY = std::max<std::int64_t>(cy - radius, std::numeric_limits<std::int32_t>::min());
    const std::int64_t maxY = std::min<std::int64_t>(cy + radius, std::numeric_limits<std::int32_t>::max());

    auto take = [&]() {
        if (result.samples >= sampleBudget) {
            result.status = VantageStatus::Truncated;
            return false;
        }
        ++result.samples;
        return true;
    };

    for (std::int64_t ty = minY; ty <= maxY; ++ty) {
        for (std::int64_t tx = minX; tx <= maxX; ++tx) {
            const std::int64_t dx = tx - cx;
            const std::int64_t dy = ty - cy;
            if (dx * dx + dy * dy > r2) {
                continue;
            }
            if (!take()) {
                return result;
            }
            float targetH = 0.0f;
            if (!sample(height, tx, ty, targetH)) {
                continue;
            }
            if (dx == 0 && dy == 0) {
                result.cells.push_back({vantage.cell.x, vantage.cell.y});
                continue;
            }

            const float targetSlope = slopeTo(eye, targetH, dx, dy);
            const std::int64_t steps = std::max(std::llabs(dx), std::llabs(dy));
            float blocking = kNegInf;
            for (std::int64_t i = 1; i < steps && blocking <= targetSlope; ++i) {
                const std::int64_t ox = roundedStep(dx, i, steps);
                const std::int64_t oy = roundedStep(dy, i, steps);
                if (!take()) {
                    return result;
                }
                float h = 0.0f;
                if (sample(height, cx + ox, cy + oy, h)) {
                    blocking = std::max(blocking, slopeTo(eye, h, ox, oy));
                }
            }
            if (targetSlope >= blocking) {
                result.cells.push_back({static_cast<std::int32_t>(tx), static_cast<std::int32_t>(ty)});
            }
        }
    }
    return result;
}

std::size_t mergeRevealedCells(std::vector<VantageCell>& mapCells, const std::vector<VantageCell>& revealed)
{
    std::sort(mapCells.begin(), mapCells.end(), vantageCellLess);
    mapCells.erase(std::unique(mapCells.begin(), mapCells.end()), mapCells.end());
    const std::size_t before = mapCells.size();

    std::vector<VantageCell> add(revealed);
    std::sort(add.begin(), add.end(), vantageCellLess);
    add.erase(std::unique(add.begin(), add.end()), add.end());

    std::vector<VantageCell> merged;
    merged.reserve(before + add.size());
    std::set_union(mapCells.begin(), mapCells.end(), add.begin(), add.end(), std::back_inserter(merged), vantageCellLess);
    mapCells = std::move(merged);
    return mapCells.size() - before;
}

}
