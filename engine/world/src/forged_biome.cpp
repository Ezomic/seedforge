#include "ichor/world/forged_biome.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace ichor {

namespace {

using worldforge::BiomeId;

// In BiomeId order.
constexpr std::array<BiomeSurface, worldforge::kBiomeCount> kSurfaces = {{
    /* Ocean       */ {0.00f, 0.00f, 0.0f, {0.05f, 0.12f, 0.20f}, {0.0f, 1.0f, 0.0f}},
    /* Lake        */ {0.00f, 0.00f, 0.0f, {0.07f, 0.16f, 0.22f}, {0.0f, 1.0f, 0.0f}},
    /* Glacier     */ {0.00f, 0.02f, 0.0f, {0.80f, 0.86f, 0.92f}, {0.0f, 0.1f, 0.9f}},
    /* Tundra      */ {0.05f, 0.08f, 1800.0f, {0.38f, 0.40f, 0.30f}, {0.3f, 0.5f, 0.2f}},
    /* Taiga       */ {0.85f, 0.07f, 2200.0f, {0.16f, 0.26f, 0.14f}, {0.7f, 0.2f, 0.1f}},
    /* Forest      */ {1.00f, 0.06f, 2600.0f, {0.20f, 0.34f, 0.12f}, {0.9f, 0.1f, 0.0f}},
    /* Grassland   */ {0.12f, 0.05f, 3000.0f, {0.36f, 0.46f, 0.18f}, {1.0f, 0.0f, 0.0f}},
    /* Desert      */ {0.00f, 0.12f, 3500.0f, {0.66f, 0.55f, 0.34f}, {0.0f, 1.0f, 0.0f}},
    /* Savanna     */ {0.15f, 0.07f, 3200.0f, {0.52f, 0.46f, 0.20f}, {0.6f, 0.4f, 0.0f}},
    /* Rainforest  */ {1.00f, 0.04f, 2800.0f, {0.10f, 0.28f, 0.08f}, {1.0f, 0.0f, 0.0f}},
    /* Alpine      */ {0.10f, 0.15f, 1400.0f, {0.42f, 0.42f, 0.40f}, {0.1f, 0.6f, 0.3f}},
    /* Crag        */ {0.00f, 0.35f, 3000.0f, {0.40f, 0.37f, 0.34f}, {0.0f, 1.0f, 0.0f}},
    /* Marsh       */ {0.25f, 0.03f, 2400.0f, {0.24f, 0.30f, 0.16f}, {0.9f, 0.1f, 0.0f}},
    /* LowlandFarm */ {0.05f, 0.02f, 3000.0f, {0.44f, 0.46f, 0.20f}, {1.0f, 0.0f, 0.0f}},
    /* SaltBlight  */ {0.00f, 0.10f, 3000.0f, {0.74f, 0.72f, 0.66f}, {0.0f, 0.9f, 0.1f}},
    /* TidalRuin   */ {0.05f, 0.08f, 2400.0f, {0.30f, 0.30f, 0.26f}, {0.3f, 0.7f, 0.0f}},
}};

bool isWater(int id)
{
    return id == static_cast<int>(BiomeId::Ocean) || id == static_cast<int>(BiomeId::Lake);
}

std::uint8_t clampedId(std::uint8_t id)
{
    return id < worldforge::kBiomeCount ? id : static_cast<std::uint8_t>(BiomeId::Crag);
}

ForgedBiomes failure(std::string message)
{
    ForgedBiomes out;
    out.error = std::move(message);
    return out;
}

}

const BiomeSurface& biomeSurface(BiomeId biome)
{
    const auto index = static_cast<std::size_t>(biome);
    return kSurfaces[index < kSurfaces.size() ? index : static_cast<std::size_t>(BiomeId::Crag)];
}

ForgedBiomeField::ForgedBiomeField(const worldforge::BiomeMap& map, std::uint32_t sizeMetres)
{
    const auto cells = static_cast<std::size_t>(std::max(map.width, 0)) * static_cast<std::size_t>(std::max(map.height, 0));
    if (map.width < 2 || map.width != map.height || sizeMetres == 0 || map.biome.size() != cells || map.secondary.size() != cells || map.blend.size() != cells) {
        return;
    }
    width_ = map.width;
    sizeMetres_ = sizeMetres;
    biome_ = map.biome;
    secondary_ = map.secondary;
    blend_ = map.blend;
}

int ForgedBiomeField::forgedCoord(double metres) const
{
    const double t = std::isfinite(metres) ? std::clamp(metres / static_cast<double>(sizeMetres_), 0.0, 1.0) : 0.0;
    return std::clamp(static_cast<int>(t * static_cast<double>(width_ - 1) + 0.5), 0, width_ - 1);
}

std::size_t ForgedBiomeField::indexAt(double x, double z) const
{
    return static_cast<std::size_t>(forgedCoord(z)) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(forgedCoord(x));
}

BiomeId ForgedBiomeField::biomeAt(double x, double z) const
{
    if (empty()) return BiomeId::Crag;
    return static_cast<BiomeId>(clampedId(biome_[indexAt(x, z)]));
}

BiomeId ForgedBiomeField::cellBiome(const WorldDesc& desc, CellId id) const
{
    if (empty()) return BiomeId::Crag;
    const double x0 = static_cast<double>(id.x) * desc.cellMetres;
    const double z0 = static_cast<double>(id.z) * desc.cellMetres;
    const int fx0 = forgedCoord(x0);
    const int fx1 = forgedCoord(x0 + desc.cellMetres);
    const int fz0 = forgedCoord(z0);
    const int fz1 = forgedCoord(z0 + desc.cellMetres);
    std::array<int, worldforge::kBiomeCount> count{};
    for (int z = fz0; z <= fz1; ++z) {
        for (int x = fx0; x <= fx1; ++x) {
            ++count[clampedId(biome_[static_cast<std::size_t>(z) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)])];
        }
    }
    int best = -1;
    int bestCount = 0;
    for (int pass = 0; pass < 2 && best < 0; ++pass) {
        for (int b = 0; b < worldforge::kBiomeCount; ++b) {
            if (pass == 0 && isWater(b)) continue;
            if (count[static_cast<std::size_t>(b)] > bestCount) {
                best = b;
                bestCount = count[static_cast<std::size_t>(b)];
            }
        }
    }
    return static_cast<BiomeId>(best < 0 ? static_cast<int>(BiomeId::Crag) : best);
}

glm::vec3 ForgedBiomeField::splatAt(double x, double z) const
{
    if (empty()) return biomeSurface(BiomeId::Crag).splat;
    const std::size_t i = indexAt(x, z);
    const float w = static_cast<float>(blend_[i]) / 255.0f;
    const glm::vec3 a = biomeSurface(static_cast<BiomeId>(clampedId(biome_[i]))).splat;
    const glm::vec3 b = biomeSurface(static_cast<BiomeId>(clampedId(secondary_[i]))).splat;
    return a * (1.0f - w) + b * w;
}

BiomeBlend ForgedBiomeField::blendAt(double x, double z) const
{
    if (empty()) return {};
    const std::size_t i = indexAt(x, z);
    return {static_cast<BiomeId>(clampedId(biome_[i])), static_cast<BiomeId>(clampedId(secondary_[i])), static_cast<float>(blend_[i]) / 255.0f};
}

glm::vec3 ForgedBiomeField::groundColourAt(double x, double z) const
{
    if (empty()) return biomeSurface(BiomeId::Crag).groundColour;
    const std::size_t i = indexAt(x, z);
    const float w = static_cast<float>(blend_[i]) / 255.0f;
    const glm::vec3 a = biomeSurface(static_cast<BiomeId>(clampedId(biome_[i]))).groundColour;
    const glm::vec3 b = biomeSurface(static_cast<BiomeId>(clampedId(secondary_[i]))).groundColour;
    return a * (1.0f - w) + b * w;
}

ForgedBiomes biomesFromForgedWorld(const worldforge::ForgedWorld& world, const WorldDesc& desc)
{
    if (!desc.valid()) {
        return failure("the world description is not valid");
    }
    if (!world.biomes().has_value()) {
        return failure(world.ok() ? "the forged world stopped before biomes" : world.error());
    }
    const worldforge::BiomeMap& map = *world.biomes();
    ForgedBiomes out;
    out.field = ForgedBiomeField(map, desc.sizeMetres);
    if (out.field.empty()) {
        return failure(std::format("the forged biome map is {}x{} with {} cells, not a consistent square grid", map.width, map.height, map.biome.size()));
    }
    return out;
}

VegetationParams vegetationParamsFor(BiomeId biome, const WorldDesc& desc, const VegetationParams& base)
{
    const BiomeSurface& surface = biomeSurface(biome);
    const float sea = desc.hasWater() ? desc.waterHeight : 0.0f;
    VegetationParams params = base;
    params.forestDensity = surface.forestDensity;
    params.rockChance = surface.rockChance;
    params.minHeight = sea + base.minHeight;
    params.maxHeight = sea + surface.snowLineM;
    return params;
}

void scatterBiomeVegetation(const Heightfield& heightfield, const ForgedBiomeField& biomes, CellId id, const VegetationParams& base, std::vector<VegetationInstance>& out)
{
    const WorldDesc& desc = heightfield.desc();
    scatterVegetation(heightfield, id, vegetationParamsFor(biomes.cellBiome(desc, id), desc, base), out);
}

}
