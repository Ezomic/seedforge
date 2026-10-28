#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// The cooked scene bundle: the only thing the runtime ever loads. The cooker
// turns glTF plus PNG into this; nothing at runtime parses source formats.

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 tangent; // w is the bitangent sign
    glm::vec2 uv;
    std::uint16_t joints[4]; // skinning; all zero with weights {1,0,0,0} for rigid meshes
    std::uint16_t weights[4]; // normalised, 65535 is 1.0
};
static_assert(sizeof(Vertex) == 64);

struct Submesh {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t material = 0;
};

constexpr std::uint32_t kNoSkeleton = 0xffffffffu;

struct Mesh {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Submesh> submeshes;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::uint32_t skeleton = kNoSkeleton;
};

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

constexpr std::uint32_t kNoTexture = 0xffffffffu;

// How many materials the renderer holds in total, across every bundle it has uploaded.
constexpr std::uint32_t kMaxSceneMaterials = 256;

struct Material {
    std::string name;
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    std::uint32_t baseColorTexture = kNoTexture;
    std::uint32_t metallicRoughnessTexture = kNoTexture;
    std::uint32_t normalTexture = kNoTexture;
};

// A skeleton built by ozz at cook time and stored as its archive bytes, plus
// what the renderer and the game need beside it.
struct Skeleton {
    std::string name;
    std::vector<std::string> jointNames; // ozz joint order
    std::vector<glm::mat4> inverseBind; // one per joint, same order
    std::vector<std::uint8_t> ozzBytes;
};

struct AnimationClip {
    std::string name;
    std::uint32_t skeleton = 0;
    float duration = 0.0f; // seconds
    std::vector<std::uint8_t> ozzBytes;
};

// Mono PCM at the source rate; the mixer resamples.
struct SoundClip {
    std::string name;
    std::uint32_t sampleRate = 0;
    std::vector<std::int16_t> samples;
};

struct Node {
    std::string name;
    std::uint32_t mesh = 0;
    glm::mat4 world{1.0f};
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

constexpr std::uint32_t kMaxTextureEdge = 16384;

// A known format, sane dimensions, the mip count those dimensions give and exactly the bytes that layout needs.
inline bool isTextureLayoutValid(const Texture& t)
{
    return t.width != 0 && t.height != 0 && t.width <= kMaxTextureEdge && t.height <= kMaxTextureEdge && isKnownTextureFormat(t.format)
        && t.mipCount == mipLevelCount(t.width, t.height) && t.pixels.size() == textureByteSize(t.format, t.width, t.height, t.mipCount);
}

}
