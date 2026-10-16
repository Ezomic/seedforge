#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace forgecli {

// 8-bit greyscale (channels 1) or RGB (channels 3). `pixels` is width * height * channels bytes,
// row-major. Encoded with stored (uncompressed) deflate blocks, so there is no compressor to get
// wrong and the bytes are identical everywhere. nullopt when the dimensions or buffer are invalid.
std::optional<std::vector<std::uint8_t>> encodePng(int width, int height, int channels, std::span<const std::uint8_t> pixels);

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0);
std::uint32_t adler32(std::span<const std::uint8_t> data);

}
