#include "ichor/world/road_surface.h"

#include "ichor/assets/bundle.h"

#include <worldforge/subseed.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ichor {

namespace {

constexpr std::uint64_t kDirtDomain = 0x6469727472643031ull;

double hashUnit(std::uint32_t x, std::uint32_t y, std::uint32_t salt)
{
    const std::uint64_t h = worldforge::splitmix64(kDirtDomain ^ (static_cast<std::uint64_t>(x) << 40) ^ (static_cast<std::uint64_t>(y) << 16) ^ salt);
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

// Value noise that repeats every `period` lattice cells in both directions, so the tile joins itself.
double tileNoise(double u, double v, std::uint32_t period, std::uint32_t salt)
{
    const double fx = u * period;
    const double fy = v * period;
    const auto x0 = static_cast<std::uint32_t>(std::floor(fx));
    const auto y0 = static_cast<std::uint32_t>(std::floor(fy));
    const double tx = fx - std::floor(fx);
    const double ty = fy - std::floor(fy);
    const double sx = tx * tx * (3.0 - 2.0 * tx);
    const double sy = ty * ty * (3.0 - 2.0 * ty);
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return hashUnit(x % period, y % period, salt); };
    const double top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const double bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return top + (bottom - top) * sy;
}

double smooth(double lo, double hi, double v)
{
    const double t = std::clamp((v - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

struct Rgb {
    double r;
    double g;
    double b;
};

Rgb mix(Rgb a, Rgb b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb scale(Rgb a, double k) { return {a.r * k, a.g * k, a.b * k}; }

std::array<std::uint8_t, 4> pixelAt(std::uint32_t size, std::uint32_t px, std::uint32_t py)
{
    const double u = (px + 0.5) / size;
    const double v = (py + 0.5) / size;
    const double broad = tileNoise(u, v, 4, 1);
    const double medium = tileNoise(u, v, 16, 2);
    const double fine = hashUnit(px, py, 3);

    Rgb colour = mix({0.42, 0.33, 0.23}, {0.50, 0.40, 0.28}, broad);
    colour = scale(colour, 0.88 + 0.24 * medium + 0.1 * (fine - 0.5));

    const double ruts = std::max(smooth(0.075, 0.0, std::abs(u - 0.29)), smooth(0.075, 0.0, std::abs(u - 0.71)));
    colour = scale(colour, 1.0 - 0.3 * ruts);

    const double middle = smooth(0.14, 0.0, std::abs(u - 0.5));
    const double tuft = smooth(0.55, 0.8, tileNoise(u, v, 12, 4) + (fine - 0.5) * 0.25);
    colour = mix(colour, {0.30, 0.34, 0.17}, middle * tuft * 0.75);

    if (fine > 0.985) colour = mix(colour, {0.62, 0.58, 0.52}, 0.7);

    const double edge = std::abs(u - 0.5) * 2.0;
    const double wear = smooth(0.62, 0.98, edge + (medium - 0.5) * 0.45 + (fine - 0.5) * 0.12);
    colour = mix(colour, {0.27, 0.31, 0.16}, wear);

    const auto byte = [](double c) { return static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0, 1.0) * 255.0)); };
    return {byte(colour.r), byte(colour.g), byte(colour.b), 255};
}

}

Texture dirtRoadTexture(std::uint32_t size)
{
    size = std::clamp<std::uint32_t>(size, 4, 1024);
    Texture texture;
    texture.name = "road_dirt";
    texture.width = size;
    texture.height = size;
    texture.mipCount = mipLevelCount(size, size);
    texture.format = TextureFormat::Rgba8Srgb;
    std::vector<std::uint8_t> level;
    level.reserve(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const auto p = pixelAt(size, x, y);
            level.insert(level.end(), p.begin(), p.end());
        }
    }
    texture.pixels = level;
    std::uint32_t width = size;
    std::uint32_t height = size;
    while (width > 1 || height > 1) {
        const std::uint32_t nextW = std::max(1u, width / 2);
        const std::uint32_t nextH = std::max(1u, height / 2);
        std::vector<std::uint8_t> next(static_cast<std::size_t>(nextW) * nextH * 4);
        for (std::uint32_t y = 0; y < nextH; ++y) {
            for (std::uint32_t x = 0; x < nextW; ++x) {
                for (std::uint32_t c = 0; c < 4; ++c) {
                    unsigned sum = 0;
                    for (std::uint32_t dy = 0; dy < 2; ++dy) {
                        for (std::uint32_t dx = 0; dx < 2; ++dx) {
                            const std::uint32_t sx = std::min(x * 2 + dx, width - 1);
                            const std::uint32_t sy = std::min(y * 2 + dy, height - 1);
                            sum += level[(static_cast<std::size_t>(sy) * width + sx) * 4 + c];
                        }
                    }
                    next[(static_cast<std::size_t>(y) * nextW + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
        texture.pixels.insert(texture.pixels.end(), next.begin(), next.end());
        level = std::move(next);
        width = nextW;
        height = nextH;
    }
    return texture;
}

}
