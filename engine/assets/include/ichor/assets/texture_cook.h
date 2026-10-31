#pragma once

#include "ichor/assets/image.h"
#include "ichor/assets/texture_budget.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

enum class TextureUse { Color, Normal, Data };

// Colour is BC7 sRGB, a normal map BC5, other data BC7 unorm. A data texture that is one grey channel in its source is BC4.
TextureFormat chooseTextureFormat(TextureUse use, bool singleChannelSource);

// A decoded image to a compressed texture: box-downsampled while its larger edge is over `maxSize` (never upscaled),
// then a full mip chain, then block compression. `format` must be one of the block-compressed formats.
bool cookCompressedTexture(DecodedImage image, TextureFormat format, std::uint32_t maxSize, Texture& out);

// An RGBA8 texture with its mip chain to the same chain in `format`.
bool compressTexture(const Texture& rgba8, TextureFormat format, Texture& out);

// A compressed texture back to RGBA8 (BC5 gets its third channel rebuilt, BC4 is repeated into grey).
// Used when the device cannot sample block-compressed formats, and by the tests.
bool decompressTexture(const Texture& compressed, Texture& out);

// What the renderer uploads: `texture` itself, or when it is block-compressed and the device cannot sample
// those formats, the RGBA8 decode of it held in `scratch`. Null when the decode fails.
const Texture* textureForDevice(const Texture& texture, bool blockCompressionSupported, Texture& scratch);

}
