#include <forgecli/forge.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <string>

#include <worldforge/region_tier_assign.h>

namespace forgecli {

namespace {

using worldforge::ForgedWorld;
using worldforge::ForgePhase;

constexpr std::uint8_t kPlatePalette[][3] = {
    {230, 25, 75}, {60, 180, 75}, {255, 225, 25}, {67, 99, 216}, {245, 130, 49}, {145, 30, 180},
    {70, 240, 240}, {240, 50, 230}, {188, 246, 12}, {250, 190, 212}, {0, 128, 128}, {220, 190, 255},
};
constexpr std::uint8_t kBiomePalette[worldforge::kBiomeCount][3] = {
    {20, 50, 120}, {60, 120, 200}, {240, 248, 255}, {170, 190, 170}, {40, 90, 70}, {30, 120, 40}, {140, 190, 70}, {225, 200, 120},
    {200, 170, 60}, {0, 90, 30}, {150, 150, 160}, {100, 90, 85}, {70, 110, 100}, {220, 210, 90}, {160, 60, 160}, {60, 150, 150},
};
constexpr std::size_t kPaletteSize = sizeof kPlatePalette / sizeof kPlatePalette[0];
constexpr int kHeightRange = worldforge::kTectonicsMaxHeightM - worldforge::kTectonicsMinHeightM;

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

std::uint8_t heightByte(int heightM)
{
    return static_cast<std::uint8_t>((heightM - worldforge::kTectonicsMinHeightM) * 255 / kHeightRange);
}

std::uint8_t clampByte(std::int64_t v)
{
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(v, 0, 255));
}

void setRgb(Layer& layer, std::size_t cell, const std::uint8_t* rgb)
{
    layer.pixels[cell * 3] = rgb[0];
    layer.pixels[cell * 3 + 1] = rgb[1];
    layer.pixels[cell * 3 + 2] = rgb[2];
}

std::size_t cellOf(int size, int x, int y)
{
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x);
}

PhaseResult layersTectonics(const ForgedWorld& world)
{
    const auto& map = *world.tectonics();
    const int size = world.size();
    Layer plates = makeLayer("plates", size, size, 3);
    Layer boundary = makeLayer("boundary", size, size, 1);
    Layer height = makeLayer("height", size, size, 1);
    for (std::size_t i = 0; i < map.plateId.size(); ++i) {
        setRgb(plates, i, kPlatePalette[map.plateId[i] % kPaletteSize]);
        boundary.pixels[i] = static_cast<std::uint8_t>(map.boundary[i] * 85);
        height.pixels[i] = heightByte(map.heightM[i]);
    }
    PhaseResult r;
    r.layers.push_back(std::move(plates));
    r.layers.push_back(std::move(boundary));
    r.layers.push_back(std::move(height));
    return r;
}

// Manhattan distance from the map centre stands in for travel distance from the start, and the
// boundary kind for danger, until real inputs exist.
PhaseResult layersRegionTiers(const ForgedWorld& world)
{
    const auto& map = *world.tectonics();
    const int size = world.size();
    worldforge::RegionTierParams params;
    params.ringWidth = static_cast<double>(size) / 8.0;
    std::vector<worldforge::RegionTierInput> inputs(map.plateId.size());
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int dx = x - size / 2;
            const int dy = y - size / 2;
            const std::size_t i = map.index(x, y);
            inputs[i].distance = static_cast<double>((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
            inputs[i].danger = map.boundary[i] / 3.0;
        }
    }
    const auto tiers = worldforge::assignRegionTiers(world.seed(), params, inputs);
    if (!tiers) return failure("invalid tier parameters");

    Layer layer = makeLayer("region_tier", size, size, 1);
    for (std::size_t i = 0; i < tiers->size(); ++i) {
        layer.pixels[i] = static_cast<std::uint8_t>((*tiers)[i] * 255 / (params.tierCount - 1));
    }
    PhaseResult r;
    r.layers.push_back(std::move(layer));
    return r;
}

PhaseResult layersErosion(const ForgedWorld& world)
{
    const auto& eroded = *world.eroded();
    const int size = world.size();
    Layer height = makeLayer("height_eroded", size, size, 1);
    for (std::size_t i = 0; i < eroded.heightM.size(); ++i) height.pixels[i] = heightByte(eroded.heightM[i]);
    PhaseResult r;
    r.layers.push_back(std::move(height));
    return r;
}

PhaseResult layersHydrology(const ForgedWorld& world)
{
    const auto& hydro = *world.hydrology();
    const int size = world.size();
    Layer flow = makeLayer("flow", size, size, 1);
    Layer rivers = makeLayer("rivers", size, size, 1);
    Layer lakes = makeLayer("lakes", size, size, 1);
    Layer height = makeLayer("height_carved", size, size, 1);
    const std::uint64_t maxAccumulation = *std::max_element(hydro.accumulation.begin(), hydro.accumulation.end());
    for (std::size_t i = 0; i < hydro.heightM.size(); ++i) {
        const std::uint64_t root = worldforge::detail::isqrtU64((static_cast<std::uint64_t>(hydro.accumulation[i]) << 16) / maxAccumulation);
        flow.pixels[i] = static_cast<std::uint8_t>(root > 255 ? 255 : root);
        rivers.pixels[i] = static_cast<std::uint8_t>(hydro.river[i] * 255);
        lakes.pixels[i] = static_cast<std::uint8_t>(hydro.lakeDepthM[i] > 255 ? 255 : hydro.lakeDepthM[i]);
        height.pixels[i] = heightByte(hydro.heightM[i]);
    }
    PhaseResult r;
    r.layers.push_back(std::move(flow));
    r.layers.push_back(std::move(rivers));
    r.layers.push_back(std::move(lakes));
    r.layers.push_back(std::move(height));
    return r;
}

PhaseResult layersClimate(const ForgedWorld& world)
{
    const auto& climate = *world.climate();
    const int size = world.size();
    Layer temperature = makeLayer("temperature", size, size, 1);
    Layer moisture = makeLayer("moisture", size, size, 1);
    for (std::size_t i = 0; i < climate.moisture.size(); ++i) {
        temperature.pixels[i] = clampByte((static_cast<std::int64_t>(climate.temperatureC10[i]) - worldforge::kClimateMinTempC10) * 255 / (worldforge::kClimateMaxTempC10 - worldforge::kClimateMinTempC10));
        moisture.pixels[i] = clampByte(static_cast<std::int64_t>(climate.moisture[i]) * 255 / worldforge::kClimateMaxMoisture);
    }
    PhaseResult r;
    r.layers.push_back(std::move(temperature));
    r.layers.push_back(std::move(moisture));
    return r;
}

PhaseResult layersBiomes(const ForgedWorld& world)
{
    const auto& biomes = *world.biomes();
    const int size = world.size();
    Layer color = makeLayer("biome", size, size, 3);
    Layer blend = makeLayer("biome_blend", size, size, 1);
    for (std::size_t i = 0; i < biomes.biome.size(); ++i) {
        setRgb(color, i, kBiomePalette[biomes.biome[i]]);
        blend.pixels[i] = biomes.blend[i];
    }
    PhaseResult r;
    r.layers.push_back(std::move(color));
    r.layers.push_back(std::move(blend));
    return r;
}

PhaseResult layersResources(const ForgedWorld& world)
{
    const auto& resources = *world.resources();
    const int size = world.size();
    Layer kind = makeLayer("ore_kind", size, size, 1);
    Layer grade = makeLayer("ore_grade", size, size, 1);
    Layer arable = makeLayer("arable", size, size, 1);
    for (std::size_t i = 0; i < resources.oreKind.size(); ++i) {
        kind.pixels[i] = clampByte(resources.oreKind[i] * 100);
        grade.pixels[i] = resources.oreGrade[i];
        arable.pixels[i] = resources.arable[i];
    }
    PhaseResult r;
    r.layers.push_back(std::move(kind));
    r.layers.push_back(std::move(grade));
    r.layers.push_back(std::move(arable));
    return r;
}

PhaseResult layersSettlements(const ForgedWorld& world)
{
    const auto& settlements = *world.settlements();
    const int size = world.size();
    Layer suitability = makeLayer("suitability", size, size, 1);
    Layer sites = makeLayer("settlements", size, size, 1);
    suitability.pixels = settlements.suitability;
    for (const worldforge::Settlement& s : settlements.sites) {
        sites.pixels[settlements.index(s.x, s.y)] = clampByte((s.tier + 1) * 64);
    }
    PhaseResult r;
    r.layers.push_back(std::move(suitability));
    r.layers.push_back(std::move(sites));
    return r;
}

PhaseResult layersRoads(const ForgedWorld& world)
{
    const auto& roads = *world.roads();
    const int size = world.size();
    Layer road = makeLayer("road", size, size, 1);
    Layer height = makeLayer("height_roads", size, size, 1);
    for (std::size_t i = 0; i < roads.road.size(); ++i) {
        road.pixels[i] = clampByte(roads.road[i] * 127);
        height.pixels[i] = heightByte(roads.heightM[i]);
    }
    PhaseResult r;
    r.layers.push_back(std::move(road));
    r.layers.push_back(std::move(height));
    return r;
}

PhaseResult layersPolitics(const ForgedWorld& world)
{
    const auto& politics = *world.politics();
    const int size = world.size();
    constexpr std::uint8_t kUnowned[3] = {40, 40, 40};
    Layer territory = makeLayer("territory", size, size, 3);
    Layer contested = makeLayer("contested", size, size, 1);
    for (std::size_t i = 0; i < politics.owner.size(); ++i) {
        setRgb(territory, i, politics.owner[i] == worldforge::kPoliticsNoOwner ? kUnowned : kPlatePalette[politics.owner[i] % kPaletteSize]);
        contested.pixels[i] = politics.contested(i) ? 255 : 0;
    }
    PhaseResult r;
    r.layers.push_back(std::move(territory));
    r.layers.push_back(std::move(contested));
    return r;
}

PhaseResult layersPois(const ForgedWorld& world)
{
    const auto& pois = *world.pois();
    const int size = world.size();
    Layer layer = makeLayer("pois", size, size, 1);
    for (const worldforge::Poi& p : pois.pois) {
        layer.pixels[cellOf(size, p.x, p.y)] = clampByte(64 + p.score * 191 / 1000);
    }
    PhaseResult r;
    r.layers.push_back(std::move(layer));
    return r;
}

// "settlement_0003" and "poi_0012" name the sites of phases 7 and 10 by index.
std::optional<std::size_t> cellOfPoiId(const ForgedWorld& world, std::string_view id)
{
    const auto index = [&](std::string_view prefix) -> std::optional<std::size_t> {
        if (id.substr(0, prefix.size()) != prefix) return std::nullopt;
        const std::string_view digits = id.substr(prefix.size());
        std::size_t n = 0;
        const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
        if (ec != std::errc() || end != digits.data() + digits.size()) return std::nullopt;
        return n;
    };
    const int size = world.size();
    if (const auto n = index("settlement_")) {
        const auto& sites = world.settlements()->sites;
        if (*n < sites.size()) return cellOf(size, sites[*n].x, sites[*n].y);
    }
    if (const auto n = index("poi_")) {
        const auto& pois = world.pois()->pois;
        if (*n < pois.size()) return cellOf(size, pois[*n].x, pois[*n].y);
    }
    return std::nullopt;
}

// One pixel per settlement and POI, brighter the more items, cast members and dungeons the manifest binds to it.
PhaseResult layersBinding(const ForgedWorld& world)
{
    const auto& manifest = *world.manifest();
    const int size = world.size();
    std::map<std::size_t, int> bound;
    for (const std::string& id : manifest.poiIds) {
        if (const auto cell = cellOfPoiId(world, id)) bound.emplace(*cell, 0);
    }
    const auto add = [&](const std::string& poiId) {
        if (const auto cell = cellOfPoiId(world, poiId)) ++bound[*cell];
    };
    for (const auto& item : manifest.items) add(item.poiId);
    for (const auto& member : manifest.cast) add(member.homePoiId);
    for (const auto& dungeon : manifest.dungeons) add(dungeon.poiId);
    Layer layer = makeLayer("content", size, size, 1);
    for (const auto& [cell, count] : bound) layer.pixels[cell] = clampByte(32 + static_cast<std::int64_t>(count) * 24);
    PhaseResult r;
    r.layers.push_back(std::move(layer));
    return r;
}

template <ForgePhase Needs, PhaseResult (*Layers)(const ForgedWorld&)>
PhaseResult runAlone(std::uint64_t seed, int size)
{
    const ForgedWorld world = worldforge::forgeWorld(seed, size, defaultForgeConfig(size), Needs);
    return phaseLayers(Phase{{}, {}, nullptr, Needs, Layers}, world);
}

}

worldforge::ForgeConfig defaultForgeConfig(int size)
{
    worldforge::ForgeConfig config;
    const int spacing = std::clamp(size / 16, 2, 24);
    config.settlements.minSpacing = spacing;
    config.pois.minSpacing = spacing;
    const std::int64_t clamped = std::clamp(size, worldforge::kTectonicsMinSize, worldforge::kTectonicsMaxSize);
    const std::int64_t area = clamped * clamped;
    config.hydrology.riverThreshold = std::max<std::int64_t>(16, config.hydrology.riverThreshold * area / (1024 * 1024));
    worldforge::FactionSpec contiguous;
    worldforge::FactionSpec ore;
    ore.strategy = worldforge::ClaimStrategy::ResourceFollowing;
    ore.oreWeight = 200;
    config.factions = {contiguous, ore};
    return config;
}

PhaseResult phaseLayers(const Phase& phase, const ForgedWorld& world)
{
    if (world.completedPhases() >= static_cast<int>(phase.needs)) return phase.layers(world);
    const auto failed = world.failedPhase();
    if (!failed) return failure("the world was forged only up to " + std::string(worldforge::forgePhaseName(world.stopAfter())));
    const std::string own = std::string(worldforge::forgePhaseName(*failed)) + ": ";
    std::string message = world.error();
    if (message.compare(0, own.size(), own) == 0) message.erase(0, own.size());
    return failure(*failed == phase.needs ? message : own + message);
}

ForgePhase highestNeed(const std::vector<const Phase*>& phases)
{
    ForgePhase need = ForgePhase::Tectonics;
    for (const Phase* p : phases) {
        if (static_cast<int>(p->needs) > static_cast<int>(need)) need = p->needs;
    }
    return need;
}

const std::vector<Phase>& phaseRegistry()
{
    using enum ForgePhase;
    static const std::vector<Phase> phases = {
        {"tectonics", "phase 1: plate map, boundary kinds and base height field", runAlone<Tectonics, layersTectonics>, Tectonics, layersTectonics},
        {"region_tiers", "difficulty tier per cell, from distance to the centre and boundary danger (derived from phase 1)", runAlone<Tectonics, layersRegionTiers>, Tectonics, layersRegionTiers},
        {"erosion", "phase 2: hydraulic and thermal erosion of the base height field", runAlone<Erosion, layersErosion>, Erosion, layersErosion},
        {"hydrology", "phase 3: eroded terrain with depressions filled: flow accumulation, rivers, lakes and carved height", runAlone<Hydrology, layersHydrology>, Hydrology, layersHydrology},
        {"climate", "phase 4: temperature and moisture, with rain shadows", runAlone<Climate, layersClimate>, Climate, layersClimate},
        {"biomes", "phase 5: biome id colour map and border blend weight, from climate and hydrology", runAlone<Biomes, layersBiomes>, Biomes, layersBiomes},
        {"resources", "phase 6: ore kind and grade on tectonic boundaries, and arable score", runAlone<Resources, layersResources>, Resources, layersResources},
        {"settlements", "phase 7: settlement suitability and the placed sites by size tier", runAlone<Settlements, layersSettlements>, Settlements, layersSettlements},
        {"roads", "phase 8: road network between settlements and the terrain deformed to it", runAlone<Roads, layersRoads>, Roads, layersRoads},
        {"politics", "phase 9: faction territory and contested cells", runAlone<Politics, layersPolitics>, Politics, layersPolitics},
        {"pois", "phase 10: points of interest scored by sightline", runAlone<Pois, layersPois>, Pois, layersPois},
        {"binding", "phase 11: content binding of items, cast and dungeons to sites (stand-alone runs use empty pools)", runAlone<Content, layersBinding>, Content, layersBinding},
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
