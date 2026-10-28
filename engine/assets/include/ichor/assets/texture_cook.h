#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/assets/image.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// art/texture_budget.json: the largest edge a texture may keep, by the folder its glTF lives in.
struct TextureBudget {
    struct Entry {
        std::string prefix; // a path under the repository root, such as "art/props/terrain/"
        std::uint32_t maxSize = 0;
    };
    std::uint32_t defaultMax = 1024; // for a path no prefix matches
    std::vector<Entry> entries;
};

// {"default": 1024, "prefixes": [{"prefix": "art/props/", "max": 1024}, ...]}. Each problem found is added to `problems`;
// the budget is left holding only the entries that were valid, and false is returned when the file cannot be read at all.
bool loadTextureBudget(const std::filesystem::path& file, TextureBudget& budget, std::vector<std::string>& problems);

// The cap for a glTF at `sourcePath`. The longest matching prefix wins. Only the part of the path from its "art/" folder on
// is compared, so an absolute path and a repository-relative one give the same answer.
std::uint32_t maxTextureSize(const TextureBudget& budget, const std::filesystem::path& sourcePath);

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
