#include "ichor/world/heightfield.h"

#include "ichor/core/hash.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

Heightfield::Heightfield(WorldDesc desc, std::vector<std::uint16_t> samples)
    : desc_(desc)
    , samples_(std::move(samples))
{
}

float Heightfield::heightAtSample(std::uint32_t sx, std::uint32_t sz) const
{
    const std::uint32_t n = desc_.samplesPerSide();
    sx = std::min(sx, n - 1);
    sz = std::min(sz, n - 1);
    return desc_.minHeight + static_cast<float>(samples_[static_cast<std::size_t>(sz) * n + sx]) * (desc_.heightRange / 65535.0f);
}

float Heightfield::heightAt(double x, double z) const
{
    if (samples_.empty()) {
        return 0.0f;
    }
    const double fx = std::clamp(x / desc_.metresPerSample, 0.0, static_cast<double>(desc_.samplesPerSide() - 1));
    const double fz = std::clamp(z / desc_.metresPerSample, 0.0, static_cast<double>(desc_.samplesPerSide() - 1));
    const auto x0 = static_cast<std::uint32_t>(std::floor(fx));
    const auto z0 = static_cast<std::uint32_t>(std::floor(fz));
    const auto tx = static_cast<float>(fx - x0);
    const auto tz = static_cast<float>(fz - z0);
    const float h00 = heightAtSample(x0, z0);
    const float h10 = heightAtSample(x0 + 1, z0);
    const float h01 = heightAtSample(x0, z0 + 1);
    const float h11 = heightAtSample(x0 + 1, z0 + 1);
    return (h00 * (1.0f - tx) + h10 * tx) * (1.0f - tz) + (h01 * (1.0f - tx) + h11 * tx) * tz;
}

glm::vec3 Heightfield::normalAt(double x, double z) const
{
    const double d = desc_.metresPerSample;
    const float hl = heightAt(x - d, z);
    const float hr = heightAt(x + d, z);
    const float hd = heightAt(x, z - d);
    const float hu = heightAt(x, z + d);
    return glm::normalize(glm::vec3(hl - hr, static_cast<float>(2.0 * d), hd - hu));
}

bool Heightfield::contains(CellId id) const
{
    const auto n = static_cast<std::int32_t>(desc_.cellsPerSide());
    return id.x >= 0 && id.z >= 0 && id.x < n && id.z < n;
}

CellId Heightfield::cellAt(double x, double z) const
{
    // Clamped well outside the world so a runaway position cannot overflow the cast.
    const double size = desc_.cellMetres;
    const double limit = static_cast<double>(desc_.cellsPerSide()) + 16.0;
    const auto index = [&](double v) {
        const double c = std::floor(v / size);
        return static_cast<std::int32_t>(std::isnan(c) ? -limit : std::clamp(c, -limit, limit));
    };
    return {index(x), index(z)};
}

bool Heightfield::cell(CellId id, Cell& out) const
{
    if (!contains(id) || samples_.empty()) {
        return false;
    }
    const std::uint32_t side = desc_.samplesPerCellSide();
    const std::uint32_t quads = side - 1;
    out.id = id;
    out.samplesPerSide = side;
    out.heights.resize(static_cast<std::size_t>(side) * side);
    out.minHeight = 1e30f;
    out.maxHeight = -1e30f;
    for (std::uint32_t z = 0; z < side; ++z) {
        for (std::uint32_t x = 0; x < side; ++x) {
            const float h = heightAtSample(static_cast<std::uint32_t>(id.x) * quads + x, static_cast<std::uint32_t>(id.z) * quads + z);
            out.heights[static_cast<std::size_t>(z) * side + x] = h;
            out.minHeight = std::min(out.minHeight, h);
            out.maxHeight = std::max(out.maxHeight, h);
        }
    }
    return true;
}

std::uint64_t Heightfield::digest() const
{
    return hash64(std::string_view(reinterpret_cast<const char*>(samples_.data()), samples_.size() * sizeof(std::uint16_t)));
}

}
