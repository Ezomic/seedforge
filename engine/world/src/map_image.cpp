#include "ichor/world/map_image.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

struct Rgb {
    float r;
    float g;
    float b;
};

Rgb mix(Rgb a, Rgb b, float t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Rgb landColour(float t)
{
    constexpr Rgb low{0.42f, 0.48f, 0.29f};
    constexpr Rgb mid{0.66f, 0.60f, 0.41f};
    constexpr Rgb high{0.85f, 0.82f, 0.75f};
    return t < 0.5f ? mix(low, mid, t * 2.0f) : mix(mid, high, (t - 0.5f) * 2.0f);
}

Rgb waterColour(float depth)
{
    constexpr Rgb shallow{0.29f, 0.48f, 0.60f};
    constexpr Rgb deep{0.11f, 0.23f, 0.36f};
    return mix(shallow, deep, depth);
}

std::uint8_t byteOf(float v)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

}

MapImage renderMapImage(const Heightfield& heightfield, std::uint32_t maxSide)
{
    const WorldDesc& desc = heightfield.desc();
    MapImage map;
    if (maxSide == 0 || !desc.valid() || !std::isfinite(desc.minHeight)) {
        return map;
    }
    const std::uint32_t samplesPerSide = desc.samplesPerSide();
    if (heightfield.samples().size() != static_cast<std::size_t>(samplesPerSide) * samplesPerSide) {
        return map;
    }
    const std::uint32_t side = std::min(samplesPerSide, maxSide);
    map.width = side;
    map.height = side;
    map.rgba.resize(static_cast<std::size_t>(side) * side * 4);

    const float range = desc.heightRange;
    const float water = desc.waterLevel();
    const glm::vec3 towardLight = glm::normalize(glm::vec3(-0.5f, 0.7f, -0.5f));
    const double metresPerPixel = static_cast<double>(desc.sizeMetres) / side;
    for (std::uint32_t py = 0; py < side; ++py) {
        for (std::uint32_t px = 0; px < side; ++px) {
            const double x = (px + 0.5) * metresPerPixel;
            const double z = (py + 0.5) * metresPerPixel;
            const float h = heightfield.heightAt(x, z);
            const float shade = 0.55f + 0.45f * std::max(glm::dot(heightfield.normalAt(x, z), towardLight), 0.0f);
            Rgb colour;
            if (desc.hasWater() && h < water) {
                colour = waterColour(std::clamp((water - h) / (range * 0.25f), 0.0f, 1.0f));
            } else {
                const Rgb land = landColour(std::clamp((h - desc.minHeight) / range, 0.0f, 1.0f));
                colour = {land.r * shade, land.g * shade, land.b * shade};
            }
            std::uint8_t* out = &map.rgba[(static_cast<std::size_t>(py) * side + px) * 4];
            out[0] = byteOf(colour.r);
            out[1] = byteOf(colour.g);
            out[2] = byteOf(colour.b);
            out[3] = 255;
        }
    }
    return map;
}

MapPoint mapPosition(const WorldDesc& desc, double x, double z)
{
    if (desc.sizeMetres == 0) {
        return {};
    }
    const double size = desc.sizeMetres;
    const auto fraction = [&](double v) { return std::isnan(v) ? 0.5f : static_cast<float>(std::clamp(v / size, 0.0, 1.0)); };
    return {fraction(x), fraction(z)};
}

}
