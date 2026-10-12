#include "ichor/assets/image.h"

#include "ichor/core/log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace ichor {

namespace {

float toLinear(std::uint8_t v)
{
    const float c = static_cast<float>(v) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

std::uint8_t toSrgb(float v)
{
    const float c = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
}

// Box filter one level down. Colour channels average in linear light when the
// texture is sRGB; alpha and data textures average as stored.
void downsample(const std::uint8_t* src, std::uint32_t sw, std::uint32_t sh, std::uint8_t* dst, std::uint32_t dw, std::uint32_t dh, bool srgb)
{
    for (std::uint32_t y = 0; y < dh; ++y) {
        for (std::uint32_t x = 0; x < dw; ++x) {
            const std::uint32_t x0 = std::min(x * 2, sw - 1);
            const std::uint32_t x1 = std::min(x * 2 + 1, sw - 1);
            const std::uint32_t y0 = std::min(y * 2, sh - 1);
            const std::uint32_t y1 = std::min(y * 2 + 1, sh - 1);
            const std::uint8_t* p[4] = {
                src + (y0 * sw + x0) * 4, src + (y0 * sw + x1) * 4,
                src + (y1 * sw + x0) * 4, src + (y1 * sw + x1) * 4};
            std::uint8_t* d = dst + (y * dw + x) * 4;
            for (int c = 0; c < 4; ++c) {
                if (srgb && c < 3) {
                    const float sum = toLinear(p[0][c]) + toLinear(p[1][c]) + toLinear(p[2][c]) + toLinear(p[3][c]);
                    d[c] = toSrgb(sum * 0.25f);
                } else {
                    const unsigned sum = p[0][c] + p[1][c] + p[2][c] + p[3][c];
                    d[c] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
    }
}

}

bool decodeTexture(const std::uint8_t* bytes, std::size_t size, TextureFormat format, Texture& out)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        logf(LogLevel::Error, "image decode failed: {}", stbi_failure_reason());
        return false;
    }

    out.width = static_cast<std::uint32_t>(width);
    out.height = static_cast<std::uint32_t>(height);
    out.format = format;
    out.mipCount = mipLevelCount(out.width, out.height);
    out.pixels.clear();

    std::uint32_t w = out.width;
    std::uint32_t h = out.height;
    std::vector<std::uint8_t> level(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(pixels);
    out.pixels.insert(out.pixels.end(), level.begin(), level.end());

    for (std::uint32_t mip = 1; mip < out.mipCount; ++mip) {
        const std::uint32_t nw = std::max(1u, w / 2);
        const std::uint32_t nh = std::max(1u, h / 2);
        std::vector<std::uint8_t> next(static_cast<std::size_t>(nw) * nh * 4);
        downsample(level.data(), w, h, next.data(), nw, nh, format == TextureFormat::Rgba8Srgb);
        out.pixels.insert(out.pixels.end(), next.begin(), next.end());
        level.swap(next);
        w = nw;
        h = nh;
    }
    return true;
}

bool writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba)
{
    const std::string file = path.string();
    return stbi_write_png(file.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba, static_cast<int>(width * 4)) != 0;
}

bool readPng(const std::filesystem::path& path, std::uint32_t& width, std::uint32_t& height, std::vector<std::uint8_t>& rgba)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (pixels == nullptr) {
        return false;
    }
    width = static_cast<std::uint32_t>(w);
    height = static_cast<std::uint32_t>(h);
    rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return true;
}

}
