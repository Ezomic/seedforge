#include "ichor/world/forged_heightfield.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

namespace ichor {

namespace {

ForgedHeightfield failure(std::string message)
{
    ForgedHeightfield out;
    out.error = std::move(message);
    return out;
}

}

WorldDesc forgedWorldDesc(WorldDesc base)
{
    base.minHeight = static_cast<float>(worldforge::kTectonicsMinHeightM);
    base.heightRange = static_cast<float>(worldforge::kTectonicsMaxHeightM - worldforge::kTectonicsMinHeightM);
    base.waterHeight = static_cast<float>(kForgedSeaLevelM - worldforge::kTectonicsMinHeightM);
    return base;
}

ForgedHeightfield heightfieldFromForgedWorld(const worldforge::ForgedWorld& world, const WorldDesc& desc)
{
    if (!desc.valid() || !std::isfinite(desc.minHeight) || !std::isfinite(desc.heightRange)) {
        return failure("the world description is not valid");
    }
    if (!world.hydrology().has_value()) {
        return failure(world.ok() ? "the forged world stopped before hydrology" : world.error());
    }
    const worldforge::HydrologyMap& map = *world.hydrology();
    const std::int64_t cells = static_cast<std::int64_t>(map.width) * map.height;
    if (map.width < 2 || map.width != map.height || static_cast<std::int64_t>(map.heightM.size()) != cells) {
        return failure(std::format("the forged terrain is {}x{} with {} heights, not a square grid", map.width, map.height, map.heightM.size()));
    }

    const std::int64_t n = desc.samplesPerSide();
    const std::int64_t span = n - 1;            // sample steps across the world
    const std::int64_t last = map.width - 1;    // cell steps across the world
    const std::int64_t weightScale = span * span;
    const double minHeight = desc.minHeight;
    const double range = desc.heightRange;

    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n * n));
    for (std::int64_t sz = 0; sz < n; ++sz) {
        const std::int64_t pz = sz * last;
        const std::int64_t z0 = std::min(pz / span, last - 1);
        const std::int64_t rz = pz - z0 * span; // in [0, span]: how far past cell z0, in 1/span cell
        for (std::int64_t sx = 0; sx < n; ++sx) {
            const std::int64_t px = sx * last;
            const std::int64_t x0 = std::min(px / span, last - 1);
            const std::int64_t rx = px - x0 * span;
            const auto at = [&](std::int64_t x, std::int64_t z) { return static_cast<std::int64_t>(map.heightM[static_cast<std::size_t>(z * map.width + x)]); };
            const std::int64_t blended = (span - rx) * (span - rz) * at(x0, z0) + rx * (span - rz) * at(x0 + 1, z0) + (span - rx) * rz * at(x0, z0 + 1)
                + rx * rz * at(x0 + 1, z0 + 1);
            const double metres = static_cast<double>(blended) / static_cast<double>(weightScale);
            const double t = std::clamp((metres - minHeight) / range, 0.0, 1.0);
            samples[static_cast<std::size_t>(sz * n + sx)] = static_cast<std::uint16_t>(t * 65535.0 + 0.5);
        }
    }

    ForgedHeightfield out;
    out.field = Heightfield(desc, std::move(samples));
    return out;
}

ForgedHeightfield forgeHeightfieldFromWorldforge(const WorldDesc& desc, int gridSize)
{
    if (gridSize < worldforge::kTectonicsMinSize || gridSize > worldforge::kTectonicsMaxSize) {
        return failure(std::format("the forged grid size {} is outside [{}, {}]", gridSize, worldforge::kTectonicsMinSize, worldforge::kTectonicsMaxSize));
    }
    const worldforge::ForgedWorld world = worldforge::forgeWorld(desc.seed, gridSize, {}, worldforge::ForgePhase::Hydrology);
    if (!world.ok()) {
        return failure(world.error());
    }
    return heightfieldFromForgedWorld(world, desc);
}

}
