#pragma once

#include "ichor/assets/bundle.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ichor {

// Decodes PNG or JPEG bytes to RGBA8 and builds the full mip chain.
bool decodeTexture(const std::uint8_t* bytes, std::size_t size, TextureFormat format, Texture& out);

// One decoded PNG or JPEG: RGBA8 whatever the file held, and how many channels the file itself had.
struct DecodedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int sourceChannels = 0;
    std::vector<std::uint8_t> rgba;
};
bool decodeImage(const std::uint8_t* bytes, std::size_t size, DecodedImage& out);

// Box-filters `image` down by halves until its larger edge is at most `maxSize`. A smaller image is left alone.
void shrinkToFit(DecodedImage& image, std::uint32_t maxSize, bool srgb);

// `image` and every box-filtered level below it, packed largest first into an RGBA8 texture of `format`.
void buildMipChain(const DecodedImage& image, TextureFormat format, Texture& out);

// RGBA8 to a PNG file. Used by the cooker's debug dumps and the golden tests.
bool writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba);
bool readPng(const std::filesystem::path& path, std::uint32_t& width, std::uint32_t& height, std::vector<std::uint8_t>& rgba);

}
