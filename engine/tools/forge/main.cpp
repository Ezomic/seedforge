#include "ichor/assets/bundle_io.h"
#include "ichor/core/log.h"
#include "ichor/world/forge.h"
#include "ichor/world/hydrology.h"
#include "ichor/world/world_io.h"

#include <charconv>
#include <filesystem>
#include <string_view>

namespace {

bool parseU32(std::string_view text, std::uint32_t& out)
{
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size();
}

}

// ichor_forge <out dir> <seed> [size] [metresPerSample] [cellMetres] [waterHeight] [riverThreshold]
// Writes manifest.iworld, heightmap.r16 and terrain.iscene. Stands in for
// libworldforge until the real phases exist; the layout is the shipped one.
int main(int argc, char** argv)
{
    if (argc < 3) {
        ichor::log(ichor::LogLevel::Info, "usage: ichor_forge <out dir> <seed> [sizeMetres] [metresPerSample] [cellMetres] [waterHeight] [riverThreshold, 0 for no rivers]");
        return 2;
    }
    ichor::WorldDesc desc;
    std::uint32_t seed = 0;
    if (!parseU32(argv[2], seed)) return 2;
    desc.seed = seed;
    if (argc > 3 && !parseU32(argv[3], desc.sizeMetres)) return 2;
    if (argc > 4 && !parseU32(argv[4], desc.metresPerSample)) return 2;
    if (argc > 5 && !parseU32(argv[5], desc.cellMetres)) return 2;
    if (argc > 6) {
        std::uint32_t water = 0;
        if (!parseU32(argv[6], water)) return 2;
        desc.waterHeight = static_cast<float>(water);
    }
    std::uint32_t riverThreshold = 0; // zero: the land is left as forged
    if (argc > 7 && !parseU32(argv[7], riverThreshold)) return 2;
    if (!desc.valid()) {
        ichor::log(ichor::LogLevel::Error, "the world description is not valid");
        return 2;
    }

    const std::filesystem::path out = argv[1];
    ichor::Heightfield heightfield = ichor::forgeHeightfield(desc);
    // A river threshold asks for channels cut along the flow of the water; without one the land is as forged.
    if (riverThreshold > 0) {
        ichor::HydrologyParams rivers;
        rivers.riverThreshold = riverThreshold;
        heightfield = ichor::carveRivers(heightfield, ichor::computeHydrology(heightfield), rivers, ichor::CarveParams{});
    }
    if (!ichor::writeWorld(out, heightfield)) {
        return 1;
    }
    if (!ichor::writeBundle(out / "terrain.iscene", ichor::forgeTerrainMaterials(desc.seed))) {
        return 1;
    }
    ichor::logf(ichor::LogLevel::Info, "forged seed {}: {} m at {} m per sample, {}x{} cells of {} m, digest {:016x}",
        desc.seed, desc.sizeMetres, desc.metresPerSample, desc.cellsPerSide(), desc.cellsPerSide(), desc.cellMetres, heightfield.digest());
    return 0;
}
