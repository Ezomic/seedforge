#pragma once

#include "ichor/assets/bundle_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// The cooked scene bundle: the only thing the runtime ever loads. The cooker
// turns glTF plus PNG into this; nothing at runtime parses source formats.
// The stable parts it is made of are in bundle_types.h; texture storage stays here.

enum class TextureFormat : std::uint32_t {
    Rgba8Srgb = 1,
    Rgba8Unorm = 2,
    Bc7Srgb = 3,
    Bc7Unorm = 4,
    Bc5Unorm = 5, // two channels, red and green; the shader rebuilds the third
    Bc4Unorm = 6, // one channel, read as grey
};

constexpr bool isBlockCompressed(TextureFormat format)
{
    return format >= TextureFormat::Bc7Srgb && format <= TextureFormat::Bc4Unorm;
}

constexpr bool isKnownTextureFormat(TextureFormat format)
{
    return format >= TextureFormat::Rgba8Srgb && format <= TextureFormat::Bc4Unorm;
}

// Bytes of one 4x4 block, or of one pixel for the uncompressed formats.
constexpr std::uint32_t texelUnitBytes(TextureFormat format)
{
    switch (format) {
    case TextureFormat::Bc4Unorm: return 8;
    case TextureFormat::Bc5Unorm:
    case TextureFormat::Bc7Srgb:
    case TextureFormat::Bc7Unorm: return 16;
    default: return 4;
    }
}

// One mip level as stored. Compressed levels are whole blocks, so a level that is not a multiple of four
// (or smaller than four) is padded up to the block grid.
constexpr std::uint64_t mipByteSize(TextureFormat format, std::uint32_t width, std::uint32_t height)
{
    if (!isBlockCompressed(format)) {
        return static_cast<std::uint64_t>(width) * height * 4;
    }
    return static_cast<std::uint64_t>((width + 3) / 4) * ((height + 3) / 4) * texelUnitBytes(format);
}

struct Texture {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipCount = 0;
    TextureFormat format = TextureFormat::Rgba8Srgb;
    std::vector<std::uint8_t> pixels; // every mip level, tightly packed, largest first; blocks for the compressed formats
};

struct SceneBundle {
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Skeleton> skeletons;
    std::vector<AnimationClip> clips;
    std::vector<SoundClip> sounds;
    std::vector<Node> nodes;
};

inline std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height)
{
    std::uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++levels;
    }
    return levels;
}

inline std::uint64_t textureByteSize(TextureFormat format, std::uint32_t width, std::uint32_t height, std::uint32_t mipCount)
{
    std::uint64_t total = 0;
    for (std::uint32_t mip = 0; mip < mipCount; ++mip) {
        total += mipByteSize(format, width, height);
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return total;
}

// The mip tail stays resident whatever the texture budget (SEED-690): every level from the first whose larger edge
// is at most this.
constexpr std::uint32_t kMipTailMaxEdge = 64;

// The first mip level of the tail, never past the last level, so every texture keeps at least its smallest mip.
inline std::uint32_t mipTailStart(std::uint32_t width, std::uint32_t height, std::uint32_t mipCount)
{
    std::uint32_t level = 0;
    while (level + 1 < mipCount && (width > kMipTailMaxEdge || height > kMipTailMaxEdge)) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++level;
    }
    return level;
}

// Bytes of the levels from `firstMip` down, as stored.
inline std::uint64_t mipRangeByteSize(TextureFormat format, std::uint32_t width, std::uint32_t height, std::uint32_t mipCount, std::uint32_t firstMip)
{
    std::uint64_t total = 0;
    for (std::uint32_t mip = 0; mip < mipCount; ++mip) {
        if (mip >= firstMip) {
            total += mipByteSize(format, width, height);
        }
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return total;
}

constexpr std::uint32_t kMaxTextureEdge = 16384;

// A known format, sane dimensions, the mip count those dimensions give and exactly the bytes that layout needs.
inline bool isTextureLayoutValid(const Texture& t)
{
    return t.width != 0 && t.height != 0 && t.width <= kMaxTextureEdge && t.height <= kMaxTextureEdge && isKnownTextureFormat(t.format)
        && t.mipCount == mipLevelCount(t.width, t.height) && t.pixels.size() == textureByteSize(t.format, t.width, t.height, t.mipCount);
}

}
