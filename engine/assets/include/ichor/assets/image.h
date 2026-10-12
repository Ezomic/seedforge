#pragma once

#include "ichor/assets/bundle.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ichor {

// Decodes PNG or JPEG bytes to RGBA8 and builds the full mip chain.
bool decodeTexture(const std::uint8_t* bytes, std::size_t size, TextureFormat format, Texture& out);

// RGBA8 to a PNG file. Used by the cooker's debug dumps and the golden tests.
bool writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba);
bool readPng(const std::filesystem::path& path, std::uint32_t& width, std::uint32_t& height, std::vector<std::uint8_t>& rgba);

}
