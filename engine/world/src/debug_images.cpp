#include "ichor/world/debug_images.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

struct Block {
    std::uint32_t x0, x1, z0, z1; // samples, end exclusive, never empty
};

MapImage blank(std::uint32_t samplesPerSide, std::uint32_t maxSide)
{
    MapImage image;
    if (samplesPerSide == 0 || maxSide == 0) {
        return image;
    }
    image.width = image.height = std::min(samplesPerSide, maxSide);
    image.rgba.assign(static_cast<std::size_t>(image.width) * image.height * 4, 255);
    return image;
}

template <typename Shade>
void fill(MapImage& image, std::uint32_t samplesPerSide, Shade shade)
{
    const std::uint64_t n = samplesPerSide;
    const std::uint64_t side = image.width;
    for (std::uint32_t py = 0; py < side; ++py) {
        for (std::uint32_t px = 0; px < side; ++px) {
            const Block block{
                static_cast<std::uint32_t>(px * n / side), static_cast<std::uint32_t>(std::max<std::uint64_t>((px + 1) * n / side, px * n / side + 1)),
                static_cast<std::uint32_t>(py * n / side), static_cast<std::uint32_t>(std::max<std::uint64_t>((py + 1) * n / side, py * n / side + 1))};
            std::uint8_t* out = &image.rgba[(static_cast<std::size_t>(py) * side + px) * 4];
            shade(block, out);
        }
    }
}

std::uint8_t byteOf(float v)
{
    return std::isfinite(v) ? static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)) : 0;
}

void setRgb(std::uint8_t* out, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    out[0] = r;
    out[1] = g;
    out[2] = b;
}

bool consistent(const Heightfield& heightfield)
{
    const std::uint32_t n = heightfield.desc().samplesPerSide();
    return heightfield.desc().valid() && heightfield.samples().size() == static_cast<std::size_t>(n) * n;
}

bool consistent(const Hydrology& hydrology)
{
    const std::size_t count = static_cast<std::size_t>(hydrology.side) * hydrology.side;
    return hydrology.side > 0 && hydrology.accumulation.size() == count;
}

float meanSample(const std::vector<std::uint16_t>& samples, std::uint32_t n, const Block& b)
{
    double sum = 0.0;
    for (std::uint32_t z = b.z0; z < b.z1; ++z) {
        for (std::uint32_t x = b.x0; x < b.x1; ++x) {
            sum += samples[static_cast<std::size_t>(z) * n + x];
        }
    }
    return static_cast<float>(sum / (static_cast<double>(b.x1 - b.x0) * (b.z1 - b.z0)));
}

}

MapImage renderHeightImage(const Heightfield& heightfield, std::uint32_t maxSide)
{
    if (maxSide == 0 || !consistent(heightfield)) {
        return {};
    }
    const std::uint32_t n = heightfield.desc().samplesPerSide();
    const auto& samples = heightfield.samples();
    const auto [lo, hi] = std::minmax_element(samples.begin(), samples.end());
    const float low = *lo;
    const float span = static_cast<float>(*hi) - low;
    MapImage image = blank(n, maxSide);
    fill(image, n, [&](const Block& b, std::uint8_t* out) {
        const std::uint8_t grey = span > 0.0f ? byteOf((meanSample(samples, n, b) - low) / span) : 128;
        setRgb(out, grey, grey, grey);
    });
    return image;
}

MapImage renderFlowImage(const Hydrology& hydrology, std::uint32_t maxSide)
{
    if (maxSide == 0 || !consistent(hydrology)) {
        return {};
    }
    const std::uint32_t n = hydrology.side;
    const std::uint32_t biggest = *std::max_element(hydrology.accumulation.begin(), hydrology.accumulation.end());
    const float scale = biggest > 1 ? 1.0f / std::log(static_cast<float>(biggest)) : 0.0f;
    MapImage image = blank(n, maxSide);
    fill(image, n, [&](const Block& b, std::uint8_t* out) {
        std::uint32_t most = 0;
        for (std::uint32_t z = b.z0; z < b.z1; ++z) {
            for (std::uint32_t x = b.x0; x < b.x1; ++x) {
                most = std::max(most, hydrology.accumulation[hydrology.index(x, z)]);
            }
        }
        const float t = most > 1 ? std::log(static_cast<float>(most)) * scale : 0.0f;
        setRgb(out, byteOf(t * t), byteOf(t * 0.9f), byteOf(0.15f + t * 0.85f));
    });
    return image;
}

MapImage renderWaterImage(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& params, std::uint32_t maxSide)
{
    if (maxSide == 0 || !consistent(heightfield) || !consistent(hydrology) || hydrology.side != heightfield.desc().samplesPerSide()
        || hydrology.lake.size() != hydrology.accumulation.size() || hydrology.filled.size() != hydrology.accumulation.size()) {
        return {};
    }
    const std::uint32_t n = hydrology.side;
    const auto& samples = heightfield.samples();
    const auto [lo, hi] = std::minmax_element(samples.begin(), samples.end());
    const float low = *lo;
    const float span = static_cast<float>(*hi) - low;
    MapImage image = blank(n, maxSide);
    fill(image, n, [&](const Block& b, std::uint8_t* out) {
        bool lake = false;
        bool river = false;
        for (std::uint32_t z = b.z0; z < b.z1; ++z) {
            for (std::uint32_t x = b.x0; x < b.x1; ++x) {
                lake = lake || isLake(hydrology, heightfield, x, z, params);
                river = river || isRiver(hydrology, hydrology.index(x, z), params);
            }
        }
        if (lake) {
            setRgb(out, 24, 64, 168);
        } else if (river) {
            setRgb(out, 70, 150, 235);
        } else {
            const std::uint8_t grey = span > 0.0f ? byteOf(0.15f + 0.55f * (meanSample(samples, n, b) - low) / span) : 90;
            setRgb(out, grey, grey, grey);
        }
    });
    return image;
}

}
