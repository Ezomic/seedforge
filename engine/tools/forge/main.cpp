#include "ichor/assets/bundle_io.h"
#include "ichor/assets/image.h"
#include "ichor/core/log.h"
#include "ichor/world/debug_images.h"
#include "ichor/world/forge.h"
#include "ichor/world/hydrology.h"
#include "ichor/world/world_io.h"

#include <charconv>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool parseU32(std::string_view text, std::uint32_t& out)
{
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size();
}

// The picture goes to a sibling file first and is renamed over the target, so a failed write never leaves half a PNG.
bool writePngAtomically(const std::filesystem::path& path, const ichor::MapImage& image)
{
    if (image.empty()) {
        return false;
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::error_code ec;
    if (!ichor::writePng(temporary, image.width, image.height, image.rgba.data())) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

bool dumpPngs(const std::filesystem::path& dir, std::uint32_t seed, const ichor::Heightfield& heightfield, const ichor::HydrologyParams& rivers)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec || !std::filesystem::is_directory(dir)) {
        ichor::logf(ichor::LogLevel::Error, "cannot write pictures into {}", dir.string());
        return false;
    }
    const ichor::Hydrology hydrology = ichor::computeHydrology(heightfield);
    const std::string suffix = "-seed" + std::to_string(seed) + ".png";
    const std::pair<const char*, std::function<ichor::MapImage()>> pictures[] = {
        {"height", [&] { return ichor::renderHeightImage(heightfield); }},
        {"flow", [&] { return ichor::renderFlowImage(hydrology); }},
        {"water", [&] { return ichor::renderWaterImage(heightfield, hydrology, rivers); }},
    };
    bool ok = true;
    for (const auto& [name, render] : pictures) {
        const std::filesystem::path path = dir / (std::string(name) + suffix);
        if (!writePngAtomically(path, render())) {
            ichor::logf(ichor::LogLevel::Error, "cannot write {} (the world files were written)", path.string());
            ok = false;
        }
    }
    if (ok && heightfield.desc().samplesPerSide() > ichor::kDebugImageMaxSide) {
        ichor::logf(ichor::LogLevel::Info, "the world has {} samples a side, so the pictures are shrunk to {}", heightfield.desc().samplesPerSide(), ichor::kDebugImageMaxSide);
    }
    return ok;
}

}

// ichor_forge <out dir> <seed> [size] [metresPerSample] [cellMetres] [waterHeight] [riverThreshold] [--dump-png <dir>]
// Writes manifest.iworld, heightmap.r16 and terrain.iscene. Stands in for
// libworldforge until the real phases exist; the layout is the shipped one.
int main(int argc, char** argv)
{
    std::vector<char*> positional{argv[0]};
    std::filesystem::path pngDir;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--dump-png") {
            if (i + 1 >= argc || !pngDir.empty()) {
                ichor::log(ichor::LogLevel::Error, "--dump-png takes one directory");
                return 2;
            }
            pngDir = argv[++i];
        } else {
            positional.push_back(argv[i]);
        }
    }
    argc = static_cast<int>(positional.size());
    argv = positional.data();
    if (argc < 3) {
        ichor::log(ichor::LogLevel::Info, "usage: ichor_forge <out dir> <seed> [sizeMetres] [metresPerSample] [cellMetres] [waterHeight] [riverThreshold, 0 for no rivers] [--dump-png <dir>]");
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
    ichor::HydrologyParams rivers;
    if (riverThreshold > 0) rivers.riverThreshold = riverThreshold;
    rivers.maxLakeDepth = ichor::kPlayableLakeDepth;
    if (!desc.valid()) {
        ichor::log(ichor::LogLevel::Error, "the world description is not valid");
        return 2;
    }

    const std::filesystem::path out = argv[1];
    ichor::Heightfield heightfield = ichor::forgeHeightfield(desc);
    // A river threshold asks for channels cut along the flow of the water; without one the land is as forged.
    if (riverThreshold > 0) {
        heightfield = ichor::carveRivers(heightfield, ichor::computeHydrology(heightfield), rivers, ichor::CarveParams{});
    }
    if (!ichor::writeWorld(out, heightfield)) {
        return 1;
    }
    if (!ichor::writeBundle(out / "terrain.iscene", ichor::forgeTerrainMaterials(desc.seed))) {
        return 1;
    }
    if (!pngDir.empty() && !dumpPngs(pngDir, desc.seed, heightfield, rivers)) {
        return 1;
    }
    ichor::logf(ichor::LogLevel::Info, "forged seed {}: {} m at {} m per sample, {}x{} cells of {} m, digest {:016x}",
        desc.seed, desc.sizeMetres, desc.metresPerSample, desc.cellsPerSide(), desc.cellsPerSide(), desc.cellMetres, heightfield.digest());
    return 0;
}
