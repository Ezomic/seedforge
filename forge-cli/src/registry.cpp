#include <forgecli/forge.h>

#include <algorithm>

#include <worldforge/biome_map.h>
#include <worldforge/hydrology_map.h>
#include <worldforge/region_tier_assign.h>
#include <worldforge/tectonics_map.h>

namespace forgecli {

namespace {

constexpr std::uint8_t kPlatePalette[][3] = {
    {230, 25, 75}, {60, 180, 75}, {255, 225, 25}, {67, 99, 216}, {245, 130, 49}, {145, 30, 180},
    {70, 240, 240}, {240, 50, 230}, {188, 246, 12}, {250, 190, 212}, {0, 128, 128}, {220, 190, 255},
};
constexpr std::uint8_t kBiomePalette[worldforge::kBiomeCount][3] = {
    {20, 50, 120}, {60, 120, 200}, {240, 248, 255}, {170, 190, 170}, {40, 90, 70}, {30, 120, 40}, {140, 190, 70}, {225, 200, 120},
    {200, 170, 60}, {0, 90, 30}, {150, 150, 160}, {100, 90, 85}, {70, 110, 100}, {220, 210, 90}, {160, 60, 160}, {60, 150, 150},
};
constexpr std::size_t kPaletteSize = sizeof kPlatePalette / sizeof kPlatePalette[0];

Layer makeLayer(const char* name, int w, int h, int channels)
{
    Layer l;
    l.name = name;
    l.width = w;
    l.height = h;
    l.channels = channels;
    l.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(channels), 0);
    return l;
}

PhaseResult failure(std::string message)
{
    PhaseResult r;
    r.error = std::move(message);
    return r;
}

std::string sizeError(int size)
{
    return "size " + std::to_string(size) + " is outside " + std::to_string(worldforge::kTectonicsMinSize) + ".."
        + std::to_string(worldforge::kTectonicsMaxSize);
}

std::optional<worldforge::TectonicMap> tectonics(std::uint64_t seed, int size)
{
    worldforge::TectonicsParams params;
    params.width = size;
    params.height = size;
    return worldforge::generateTectonics(seed, params);
}

PhaseResult runTectonics(std::uint64_t seed, int size)
{
    const auto map = tectonics(seed, size);
    if (!map) return failure(sizeError(size));

    Layer plates = makeLayer("plates", size, size, 3);
    Layer boundary = makeLayer("boundary", size, size, 1);
    Layer height = makeLayer("height", size, size, 1);
    const int range = worldforge::kTectonicsMaxHeightM - worldforge::kTectonicsMinHeightM;
    for (std::size_t i = 0; i < map->plateId.size(); ++i) {
        const auto* c = kPlatePalette[map->plateId[i] % kPaletteSize];
        plates.pixels[i * 3] = c[0];
        plates.pixels[i * 3 + 1] = c[1];
        plates.pixels[i * 3 + 2] = c[2];
        boundary.pixels[i] = static_cast<std::uint8_t>(map->boundary[i] * 85);
        height.pixels[i] = static_cast<std::uint8_t>((map->heightM[i] - worldforge::kTectonicsMinHeightM) * 255 / range);
    }
    PhaseResult r;
    r.layers.push_back(std::move(plates));
    r.layers.push_back(std::move(boundary));
    r.layers.push_back(std::move(height));
    return r;
}

// Manhattan distance from the map centre stands in for travel distance from the start, and the
// boundary kind for danger, until real inputs exist.
PhaseResult runRegionTiers(std::uint64_t seed, int size)
{
    const auto map = tectonics(seed, size);
    if (!map) return failure(sizeError(size));

    worldforge::RegionTierParams params;
    params.ringWidth = static_cast<double>(size) / 8.0;
    std::vector<worldforge::RegionTierInput> inputs(map->plateId.size());
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int dx = x - size / 2;
            const int dy = y - size / 2;
            const std::size_t i = map->index(x, y);
            inputs[i].distance = static_cast<double>((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
            inputs[i].danger = map->boundary[i] / 3.0;
        }
    }
    const auto tiers = worldforge::assignRegionTiers(seed, params, inputs);
    if (!tiers) return failure("invalid tier parameters");

    Layer layer = makeLayer("region_tier", size, size, 1);
    for (std::size_t i = 0; i < tiers->size(); ++i) {
        layer.pixels[i] = static_cast<std::uint8_t>((*tiers)[i] * 255 / (params.tierCount - 1));
    }
    PhaseResult r;
    r.layers.push_back(std::move(layer));
    return r;
}

PhaseResult runHydrology(std::uint64_t seed, int size)
{
    const auto map = tectonics(seed, size);
    if (!map) return failure(sizeError(size));
    const auto eroded = worldforge::erodeTectonics(*map);
    if (!eroded) return failure("erosion failed");
    const auto hydro = worldforge::computeHydrology(*eroded);
    if (!hydro) return failure("hydrology failed");

    Layer flow = makeLayer("flow", size, size, 1);
    Layer rivers = makeLayer("rivers", size, size, 1);
    Layer lakes = makeLayer("lakes", size, size, 1);
    Layer height = makeLayer("height_carved", size, size, 1);
    const int range = worldforge::kTectonicsMaxHeightM - worldforge::kTectonicsMinHeightM;
    const std::uint64_t maxAccumulation = *std::max_element(hydro->accumulation.begin(), hydro->accumulation.end());
    for (std::size_t i = 0; i < hydro->heightM.size(); ++i) {
        const std::uint64_t root = worldforge::detail::isqrtU64((static_cast<std::uint64_t>(hydro->accumulation[i]) << 16) / maxAccumulation);
        flow.pixels[i] = static_cast<std::uint8_t>(root > 255 ? 255 : root);
        rivers.pixels[i] = static_cast<std::uint8_t>(hydro->river[i] * 255);
        lakes.pixels[i] = static_cast<std::uint8_t>(hydro->lakeDepthM[i] > 255 ? 255 : hydro->lakeDepthM[i]);
        height.pixels[i] = static_cast<std::uint8_t>((hydro->heightM[i] - worldforge::kTectonicsMinHeightM) * 255 / range);
    }
    PhaseResult r;
    r.layers.push_back(std::move(flow));
    r.layers.push_back(std::move(rivers));
    r.layers.push_back(std::move(lakes));
    r.layers.push_back(std::move(height));
    return r;
}

PhaseResult runBiomes(std::uint64_t seed, int size)
{
    const auto map = tectonics(seed, size);
    if (!map) return failure(sizeError(size));
    const auto eroded = worldforge::erodeTectonics(*map);
    if (!eroded) return failure("erosion failed");
    const auto hydro = worldforge::computeHydrology(*eroded);
    if (!hydro) return failure("hydrology failed");
    const auto climate = worldforge::generateClimate(*eroded);
    if (!climate) return failure("climate failed");
    const auto biomes = worldforge::generateBiomes({ &*climate, &*hydro });
    if (!biomes) return failure("biomes failed");

    Layer color = makeLayer("biome", size, size, 3);
    Layer blend = makeLayer("biome_blend", size, size, 1);
    for (std::size_t i = 0; i < biomes->biome.size(); ++i) {
        const auto* c = kBiomePalette[biomes->biome[i]];
        color.pixels[i * 3] = c[0];
        color.pixels[i * 3 + 1] = c[1];
        color.pixels[i * 3 + 2] = c[2];
        blend.pixels[i] = biomes->blend[i];
    }
    PhaseResult r;
    r.layers.push_back(std::move(color));
    r.layers.push_back(std::move(blend));
    return r;
}

}

const std::vector<Phase>& phaseRegistry()
{
    static const std::vector<Phase> phases = {
        {"tectonics", "plate map, boundary kinds and base height field", runTectonics},
        {"region_tiers", "difficulty tier per cell, from distance to the centre and boundary danger", runRegionTiers},
        {"hydrology", "eroded terrain with depressions filled: flow accumulation, rivers, lakes and carved height", runHydrology},
        {"biomes", "biome id colour map and border blend weight, from climate and hydrology", runBiomes},
    };
    return phases;
}

const Phase* findPhase(std::string_view name)
{
    for (const Phase& p : phaseRegistry()) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

}
