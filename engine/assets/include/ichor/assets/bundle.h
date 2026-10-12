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
};
static_assert(sizeof(Vertex) == 48);

struct Submesh {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t material = 0;
};

struct Mesh {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Submesh> submeshes;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

enum class TextureFormat : std::uint32_t {
    Rgba8Srgb = 1,
    Rgba8Unorm = 2,
};

struct Texture {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipCount = 0;
    TextureFormat format = TextureFormat::Rgba8Srgb;
    std::vector<std::uint8_t> pixels; // every mip level, tightly packed, largest first
};

constexpr std::uint32_t kNoTexture = 0xffffffffu;

struct Material {
    std::string name;
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    std::uint32_t baseColorTexture = kNoTexture;
    std::uint32_t metallicRoughnessTexture = kNoTexture;
    std::uint32_t normalTexture = kNoTexture;
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

}
