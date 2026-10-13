#include "ichor/world/water_field.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace ichor {

WaterField::WaterField(const Heightfield& heightfield, std::shared_ptr<const Hydrology> hydrology, const HydrologyParams& rivers, const WaterMeshParams& mesh)
    : heightfield_(heightfield)
    , hydrology_(std::move(hydrology))
    , rivers_(rivers)
    , mesh_(mesh)
{
    if (!hydrology_) {
        hydrology_ = std::make_shared<const Hydrology>();
    }
}

WaterField::Sample WaterField::at(double x, double z) const
{
    Sample out;
    const WorldDesc& desc = heightfield_.desc();
    // Nothing is known about a point that is not a number, and nothing is read for one.
    if (!std::isfinite(x) || !std::isfinite(z) || heightfield_.empty()) {
        return out;
    }
    out.surface = heightfield_.heightAt(x, z);
    if (hydrology_->empty() || hydrology_->side != desc.samplesPerSide()) {
        return out;
    }
    const double metres = static_cast<double>(desc.metresPerSample);
    const double fx = x / metres;
    const double fz = z / metres;
    const double last = static_cast<double>(hydrology_->side - 1);
    if (!(fx >= 0.0 && fz >= 0.0 && fx <= last && fz <= last)) {
        return out;
    }
    const auto x0 = static_cast<std::uint32_t>(std::floor(fx));
    const auto z0 = static_cast<std::uint32_t>(std::floor(fz));
    const std::uint32_t x1 = std::min(x0 + 1, hydrology_->side - 1);
    const std::uint32_t z1 = std::min(z0 + 1, hydrology_->side - 1);
    const auto tx = static_cast<float>(fx - static_cast<double>(x0));
    const auto tz = static_cast<float>(fz - static_cast<double>(z0));
    const auto corner = [&](std::uint32_t sx, std::uint32_t sz) {
        return waterLevelAtSample(heightfield_, *hydrology_, rivers_, mesh_, sx, sz);
    };
    const WaterLevel a = corner(x0, z0);
    const WaterLevel b = corner(x1, z0);
    const WaterLevel c = corner(x0, z1);
    const WaterLevel d = corner(x1, z1);
    if (!a.wet && !b.wet && !c.wet && !d.wet) {
        return out;
    }
    // The surface the mesh draws, blended across the cell, and how far it stands over the ground.
    const float level = (a.level * (1.0f - tx) + b.level * tx) * (1.0f - tz) + (c.level * (1.0f - tx) + d.level * tx) * tz;
    const float depth = level - out.surface;
    if (depth > 0.0f) {
        out.depth = depth;
        out.surface = level;
    }
    return out;
}

}
