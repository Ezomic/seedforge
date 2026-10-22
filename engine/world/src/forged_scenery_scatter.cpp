#include "ichor/world/forged_scenery_scatter.h"

#include "ichor/core/hash.h"
#include "ichor/core/rng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace ichor {

namespace {

using worldforge::BiomeId;

constexpr float kTwoPi = 6.28318530718f;
constexpr float kPatchScaleM = 90.0f;

// In BiomeId order.
constexpr std::array<BiomeScenery, worldforge::kBiomeCount> kScenery = {{
    //                grass  bush   shrub  cactus reed   stone  deadTree conifer deadShare
    /* Ocean       */ {0.00f, 0.00f, 0.00f, 0.000f, 0.00f, 0.00f, 0.00f, 0.0f, 0.0f},
    /* Lake        */ {0.00f, 0.00f, 0.00f, 0.000f, 0.00f, 0.00f, 0.00f, 0.0f, 0.0f},
    /* Glacier     */ {0.00f, 0.00f, 0.00f, 0.000f, 0.00f, 0.02f, 0.00f, 0.0f, 0.0f},
    /* Tundra      */ {0.06f, 0.00f, 0.05f, 0.000f, 0.00f, 0.04f, 0.10f, 0.3f, 0.5f},
    /* Taiga       */ {0.15f, 0.08f, 0.02f, 0.000f, 0.00f, 0.01f, 0.03f, 0.95f, 0.03f},
    /* Forest      */ {0.30f, 0.12f, 0.00f, 0.000f, 0.00f, 0.01f, 0.01f, 0.30f, 0.02f},
    /* Grassland   */ {0.45f, 0.05f, 0.00f, 0.000f, 0.00f, 0.01f, 0.00f, 0.10f, 0.00f},
    /* Desert      */ {0.00f, 0.00f, 0.06f, 0.035f, 0.00f, 0.03f, 0.02f, 0.0f, 0.0f},
    /* Savanna     */ {0.35f, 0.03f, 0.06f, 0.000f, 0.00f, 0.01f, 0.015f, 0.0f, 0.15f},
    /* Rainforest  */ {0.35f, 0.25f, 0.00f, 0.000f, 0.00f, 0.00f, 0.01f, 0.0f, 0.0f},
    /* Alpine      */ {0.12f, 0.00f, 0.04f, 0.000f, 0.00f, 0.04f, 0.02f, 0.85f, 0.05f},
    /* Crag        */ {0.01f, 0.00f, 0.03f, 0.000f, 0.00f, 0.07f, 0.03f, 0.0f, 0.0f},
    /* Marsh       */ {0.30f, 0.06f, 0.00f, 0.000f, 0.18f, 0.00f, 0.05f, 0.0f, 0.30f},
    /* LowlandFarm */ {0.35f, 0.02f, 0.00f, 0.000f, 0.00f, 0.00f, 0.00f, 0.0f, 0.00f},
    /* SaltBlight  */ {0.00f, 0.00f, 0.05f, 0.000f, 0.00f, 0.05f, 0.06f, 0.0f, 1.00f},
    /* TidalRuin   */ {0.05f, 0.00f, 0.03f, 0.000f, 0.10f, 0.06f, 0.04f, 0.0f, 0.00f},
}};

float smoothstep(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::uint32_t kindValue(VegetationKind kind)
{
    return static_cast<std::uint32_t>(kind);
}

std::uint64_t positionKey(float x, float y, float z)
{
    std::uint32_t bits[3];
    std::memcpy(&bits[0], &x, sizeof(float));
    std::memcpy(&bits[1], &y, sizeof(float));
    std::memcpy(&bits[2], &z, sizeof(float));
    return hashCombine(hashCombine(bits[0], bits[1]), bits[2]);
}

// The trees the tree scatter planted keep their places and their count, but take the species of the biome they
// stand in; the ones on a road, in a settlement or in the water are dropped.
void speciesPass(const Heightfield& heightfield, const ForgedBiomeField& biomes, const SceneryGuard& guard, CellId id, std::vector<VegetationInstance>& plants)
{
    const double originX = heightfield.cellOriginX(id);
    const double originZ = heightfield.cellOriginZ(id);
    std::size_t kept = 0;
    for (const VegetationInstance& plant : plants) {
        const double x = originX + plant.position.x;
        const double z = originZ + plant.position.z;
        if (guard.blocked && guard.blocked(x, z)) {
            continue;
        }
        VegetationInstance next = plant;
        if (plant.kind == kindValue(VegetationKind::Conifer) || plant.kind == kindValue(VegetationKind::Broadleaf)) {
            const BiomeId biome = biomes.biomeAt(x, z);
            if (biomeSurface(biome).forestDensity <= 0.0f) {
                continue;
            }
            const BiomeScenery& scenery = biomeScenery(biome);
            Rng rng(hashCombine(heightfield.desc().seed ^ 0x7370656369657300ULL, positionKey(plant.position.x, plant.position.y, plant.position.z)));
            const float roll = rng.nextFloat();
            if (roll < scenery.deadShare) {
                next.kind = kindValue(VegetationKind::DeadTree);
            } else {
                const float conifer = (roll - scenery.deadShare) / std::max(1.0f - scenery.deadShare, 1e-3f);
                next.kind = conifer < scenery.coniferShare ? kindValue(VegetationKind::Conifer) : kindValue(VegetationKind::Broadleaf);
            }
        }
        plants[kept++] = next;
    }
    plants.resize(kept);
}

bool nearWater(const SceneryGuard& guard, double x, double z)
{
    if (!guard.wet) {
        return false;
    }
    for (const double reach : {1.5, static_cast<double>(kReedShoreM)}) {
        if (guard.wet(x + reach, z) || guard.wet(x - reach, z) || guard.wet(x, z + reach) || guard.wet(x, z - reach)) return true;
    }
    return false;
}

void coverPass(const Heightfield& heightfield, const ForgedBiomeField& biomes, const SceneryGuard& guard, CellId id, const VegetationParams& base, std::uint64_t cellSeed,
    std::vector<VegetationInstance>& out)
{
    const WorldDesc& desc = heightfield.desc();
    const double originX = heightfield.cellOriginX(id);
    const double originZ = heightfield.cellOriginZ(id);
    const auto cellMetres = static_cast<float>(desc.cellMetres);
    const auto steps = static_cast<int>(std::ceil(cellMetres / kCoverSpacingM));
    const std::uint64_t seed = hashCombine(cellSeed, 0x636f766572ULL);
    for (int gz = 0; gz < steps; ++gz) {
        for (int gx = 0; gx < steps; ++gx) {
            Rng rng(hashCombine(seed, static_cast<std::uint64_t>(gz) * 4096 + static_cast<std::uint64_t>(gx)));
            const float localX = (static_cast<float>(gx) + rng.nextFloat()) * kCoverSpacingM;
            const float localZ = (static_cast<float>(gz) + rng.nextFloat()) * kCoverSpacingM;
            const float roll = rng.nextFloat();
            const float yaw = rng.nextFloat() * kTwoPi;
            const float sizeRoll = rng.nextFloat();
            if (localX >= cellMetres || localZ >= cellMetres) {
                continue;
            }
            const double worldX = originX + localX;
            const double worldZ = originZ + localZ;
            const BiomeId biome = biomes.biomeAt(worldX, worldZ);
            const BiomeScenery& scenery = biomeScenery(biome);
            const float total = scenery.grass + scenery.bush + scenery.shrub + scenery.cactus + scenery.reed + scenery.stone;
            if (roll >= total) {
                continue;
            }
            const VegetationParams params = vegetationParamsFor(biome, desc, base);
            const float height = heightfield.heightAt(worldX, worldZ);
            const float above = height - desc.minHeight;
            if (above < params.minHeight || above > params.maxHeight) {
                continue;
            }
            const float slope = 1.0f - heightfield.normalAt(worldX, worldZ).y;
            const float level = 1.0f - smoothstep(0.1f, 0.35f, slope);
            const float patch = 0.35f + 0.65f * forestPatch(worldX, worldZ, kPatchScaleM);

            struct Choice {
                VegetationKind kind;
                float chance;
            };
            const std::array<Choice, 6> choices = {{
                {VegetationKind::Grass, scenery.grass * level * patch},
                {VegetationKind::Bush, scenery.bush * level * patch},
                {VegetationKind::DryShrub, scenery.shrub * level},
                {VegetationKind::Cactus, scenery.cactus * level},
                {VegetationKind::Reed, scenery.reed},
                {VegetationKind::Rock, scenery.stone},
            }};
            // Walk the raw chances so a candidate's species does not move when the slope scales another one down.
            float cursor = 0.0f;
            const std::array<float, 6> raw = {scenery.grass, scenery.bush, scenery.shrub, scenery.cactus, scenery.reed, scenery.stone};
            int picked = -1;
            for (std::size_t i = 0; i < raw.size(); ++i) {
                cursor += raw[i];
                if (roll < cursor) {
                    picked = static_cast<int>(i);
                    break;
                }
            }
            if (picked < 0) {
                continue;
            }
            const Choice& choice = choices[static_cast<std::size_t>(picked)];
            const float rawChance = raw[static_cast<std::size_t>(picked)];
            const float keep = rawChance > 0.0f ? std::min(choice.chance / rawChance, 1.0f) : 0.0f;
            if (rng.nextFloat() >= keep) {
                continue;
            }
            if (guard.blocked && guard.blocked(worldX, worldZ)) {
                continue;
            }
            if (choice.kind == VegetationKind::Reed && !nearWater(guard, worldX, worldZ)) {
                continue;
            }
            VegetationInstance plant;
            plant.position = {localX, height, localZ};
            plant.yaw = yaw;
            plant.kind = kindValue(choice.kind);
            switch (choice.kind) {
            case VegetationKind::Rock: plant.scale = 0.3f + sizeRoll * sizeRoll * 0.9f; break;
            case VegetationKind::Grass: plant.scale = 1.1f + sizeRoll * 0.9f; break;
            case VegetationKind::Reed: plant.scale = 0.8f + sizeRoll * 0.7f; break;
            default: plant.scale = 0.7f + sizeRoll * 0.6f; break;
            }
            out.push_back(plant);
        }
    }
}

void deadTreePass(const Heightfield& heightfield, const ForgedBiomeField& biomes, const SceneryGuard& guard, CellId id, const VegetationParams& base, std::uint64_t cellSeed,
    std::vector<VegetationInstance>& out)
{
    const WorldDesc& desc = heightfield.desc();
    const double originX = heightfield.cellOriginX(id);
    const double originZ = heightfield.cellOriginZ(id);
    const auto cellMetres = static_cast<float>(desc.cellMetres);
    const auto steps = static_cast<int>(std::ceil(cellMetres / kDeadTreeSpacingM));
    const std::uint64_t seed = hashCombine(cellSeed, 0x64656164ULL);
    for (int gz = 0; gz < steps; ++gz) {
        for (int gx = 0; gx < steps; ++gx) {
            Rng rng(hashCombine(seed, static_cast<std::uint64_t>(gz) * 4096 + static_cast<std::uint64_t>(gx)));
            const float localX = (static_cast<float>(gx) + rng.nextFloat()) * kDeadTreeSpacingM;
            const float localZ = (static_cast<float>(gz) + rng.nextFloat()) * kDeadTreeSpacingM;
            const float roll = rng.nextFloat();
            const float yaw = rng.nextFloat() * kTwoPi;
            const float sizeRoll = rng.nextFloat();
            if (localX >= cellMetres || localZ >= cellMetres) {
                continue;
            }
            const double worldX = originX + localX;
            const double worldZ = originZ + localZ;
            const BiomeId biome = biomes.biomeAt(worldX, worldZ);
            if (roll >= biomeScenery(biome).deadTree) {
                continue;
            }
            const VegetationParams params = vegetationParamsFor(biome, desc, base);
            const float height = heightfield.heightAt(worldX, worldZ);
            const float above = height - desc.minHeight;
            if (above < params.minHeight || above > params.maxHeight || 1.0f - heightfield.normalAt(worldX, worldZ).y > params.maxSlope) {
                continue;
            }
            if (guard.blocked && guard.blocked(worldX, worldZ)) {
                continue;
            }
            VegetationInstance tree;
            tree.position = {localX, height, localZ};
            tree.yaw = yaw;
            tree.scale = 0.75f + sizeRoll * 0.7f;
            tree.kind = kindValue(VegetationKind::DeadTree);
            out.push_back(tree);
        }
    }
}

}

const BiomeScenery& biomeScenery(BiomeId biome)
{
    const auto index = static_cast<std::size_t>(biome);
    return kScenery[index < kScenery.size() ? index : static_cast<std::size_t>(BiomeId::Crag)];
}

void scatterForgedScenery(const Heightfield& heightfield, const ForgedBiomeField& biomes, const SceneryGuard& guard, CellId id, const VegetationParams& base,
    std::vector<VegetationInstance>& out)
{
    scatterBiomeVegetation(heightfield, biomes, id, base, out);
    speciesPass(heightfield, biomes, guard, id, out);
    const WorldDesc& desc = heightfield.desc();
    const std::uint64_t cellSeed = hashCombine(hashCombine(desc.seed ^ 0x7363656e657279ULL, static_cast<std::uint64_t>(static_cast<std::uint32_t>(id.x))), static_cast<std::uint64_t>(static_cast<std::uint32_t>(id.z)));
    coverPass(heightfield, biomes, guard, id, base, cellSeed, out);
    deadTreePass(heightfield, biomes, guard, id, base, cellSeed, out);
}

}
