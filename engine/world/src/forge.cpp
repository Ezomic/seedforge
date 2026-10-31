#include "ichor/world/forge.h"

#include "ichor/assets/bundle.h"
#include "ichor/core/hash.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

// Lattice value from integer coordinates and a seed, in [0, 1).
float lattice(std::uint64_t seed, std::int64_t x, std::int64_t z)
{
    std::uint64_t h = hashCombine(hashCombine(seed, static_cast<std::uint64_t>(x) * 0x9e3779b97f4a7c15ull), static_cast<std::uint64_t>(z) * 0xc2b2ae3d27d4eb4full);
    // Finalise so every input bit reaches every output bit; hashCombine alone leaves a small seed in the low bits.
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return static_cast<float>(h & 0xffffff) / 16777216.0f;
}

float smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float valueNoise(std::uint64_t seed, float x, float z)
{
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const auto ix = static_cast<std::int64_t>(fx);
    const auto iz = static_cast<std::int64_t>(fz);
    const float tx = smooth(x - fx);
    const float tz = smooth(z - fz);
    const float a = lattice(seed, ix, iz);
    const float b = lattice(seed, ix + 1, iz);
    const float c = lattice(seed, ix, iz + 1);
    const float d = lattice(seed, ix + 1, iz + 1);
    return (a * (1.0f - tx) + b * tx) * (1.0f - tz) + (c * (1.0f - tx) + d * tx) * tz;
}

float fbm(std::uint64_t seed, float x, float z, int octaves)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * valueNoise(seed + static_cast<std::uint64_t>(i) * 7919u, x * frequency, z * frequency);
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum; // roughly [0, 1)
}

}

Heightfield forgeHeightfield(const WorldDesc& desc)
{
    const std::uint32_t n = desc.samplesPerSide();
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n) * n);
    const float size = static_cast<float>(desc.sizeMetres);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            const float wx = static_cast<float>(x * desc.metresPerSample);
            const float wz = static_cast<float>(z * desc.metresPerSample);
            // Continental shape: a soft dome so the coast is near the edges, ridged noise on top.
            const float u = wx / size * 2.0f - 1.0f;
            const float v = wz / size * 2.0f - 1.0f;
            const float dome = std::max(0.0f, 1.0f - (u * u + v * v) * 0.9f);
            const float base = fbm(desc.seed, wx / 1400.0f, wz / 1400.0f, 6);
            const float ridges = 1.0f - std::abs(fbm(desc.seed ^ 0x51ed270b, wx / 500.0f, wz / 500.0f, 4) * 2.0f - 1.0f);
            float height = dome * (0.35f * base + 0.45f * ridges * ridges + 0.1f);
            height = std::clamp(height, 0.0f, 1.0f);
            samples[static_cast<std::size_t>(z) * n + x] = static_cast<std::uint16_t>(std::lround(height * 65535.0f));
        }
    }
    return Heightfield(desc, std::move(samples));
}

SceneBundle forgeTerrainMaterials(std::uint64_t seed, std::uint32_t size)
{
    SceneBundle bundle;
    struct Layer {
        const char* name;
        glm::vec3 base;
        glm::vec3 tint;
        float scale;
    };
    const Layer layers[3] = {
        {"grass", {0.30f, 0.42f, 0.16f}, {0.18f, 0.28f, 0.10f}, 9.0f},
        {"rock", {0.46f, 0.42f, 0.38f}, {0.30f, 0.27f, 0.25f}, 5.0f},
        {"snow", {0.92f, 0.94f, 0.97f}, {0.78f, 0.82f, 0.90f}, 13.0f},
    };
    for (std::uint32_t i = 0; i < 3; ++i) {
        const Layer& layer = layers[i];
        Texture texture;
        texture.name = layer.name;
        texture.width = size;
        texture.height = size;
        texture.format = TextureFormat::Rgba8Srgb;
        texture.mipCount = mipLevelCount(size, size);
        std::uint32_t w = size;
        std::uint32_t h = size;
        for (std::uint32_t mip = 0; mip < texture.mipCount; ++mip) {
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    // Sampled in texture space so every mip is a filtered view of the same field.
                    const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(w) * layer.scale;
                    const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(h) * layer.scale;
                    const float n = fbm(seed + i * 131u, fx, fy, 4);
                    const glm::vec3 c = layer.base * (1.0f - n) + layer.tint * n;
                    texture.pixels.push_back(static_cast<std::uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f));
                    texture.pixels.push_back(static_cast<std::uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f));
                    texture.pixels.push_back(static_cast<std::uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f));
                    texture.pixels.push_back(255);
                }
            }
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        bundle.textures.push_back(std::move(texture));
        Material material;
        material.name = layer.name;
        material.baseColorTexture = i;
        material.metallicFactor = 0.0f;
        material.roughnessFactor = 0.95f;
        bundle.materials.push_back(material);
    }
    return bundle;
}

}
