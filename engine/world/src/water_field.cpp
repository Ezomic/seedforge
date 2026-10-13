#include "ichor/world/water_field.h"

#include <algorithm>
#include <cmath>

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

float WaterField::depthAtSample(std::uint32_t sx, std::uint32_t sz) const
{
    const Hydrology& h = *hydrology_;
    if (h.empty() || sx >= h.side || sz >= h.side) {
        return 0.0f;
    }
    const std::size_t at = h.index(sx, sz);
    if (isLake(h, heightfield_, sx, sz, rivers_)) {
        return h.filled[at] - heightfield_.heightAtSample(sx, sz);
    }
    // The meshes draw no river over the floor of a basin too deep to be a lake, and neither is there one here.
    if (h.lake[at] == 0 && isRiver(h, at, rivers_)) {
        return mesh_.riverSurface;
    }
    return 0.0f;
}

WaterField::Sample WaterField::at(double x, double z) const
{
    Sample out;
    const WorldDesc& desc = heightfield_.desc();
    out.surface = heightfield_.heightAt(x, z);
    if (heightfield_.empty() || hydrology_->empty() || hydrology_->side != desc.samplesPerSide()) {
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
    const float top = depthAtSample(x0, z0) * (1.0f - tx) + depthAtSample(x1, z0) * tx;
    const float bottom = depthAtSample(x0, z1) * (1.0f - tx) + depthAtSample(x1, z1) * tx;
    out.depth = top * (1.0f - tz) + bottom * tz;
    out.surface += out.depth;
    return out;
}

}
