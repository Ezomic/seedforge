#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <string_view>
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

// A named attachment point on a piece, in the piece's own space (docs/art-pipeline.md).
// `piece` is empty for a socket that hangs off no mesh; its transform is then file space.
struct Socket {
    std::string piece;
    std::string name; // without the SOCKET_ prefix
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct LodLevel {
    std::uint32_t mesh = 0;
    float distance = 0.0f; // the level is used from this camera distance on
};

// levels[n] is LODn; distances never decrease and levels[0].distance is 0.
struct LodGroup {
    std::string piece;
    std::vector<LodLevel> levels;
};

enum class CollisionKind : std::uint32_t {
    Box = 1, // UBX_<piece>: boundsMin/boundsMax are the box, points is empty
    Convex = 2, // UCX_<piece>: points are the hull candidates, bounds enclose them
};

struct CollisionShape {
    std::string piece;
    std::string name; // the full node name, UBX_/UCX_ included
    CollisionKind kind = CollisionKind::Box;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::vector<glm::vec3> points; // piece space
};

struct SceneBundle {
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Skeleton> skeletons;
    std::vector<AnimationClip> clips;
    std::vector<SoundClip> sounds;
    std::vector<Node> nodes;
    std::vector<Socket> sockets;
    std::vector<LodGroup> lodGroups;
    std::vector<CollisionShape> collision;
};

// A socket of `piece`, or of no piece when the piece has none by that name.
inline const Socket* findSocket(const SceneBundle& bundle, std::string_view piece, std::string_view name)
{
    const Socket* fileLevel = nullptr;
    for (const Socket& socket : bundle.sockets) {
        if (socket.name != name) continue;
        if (socket.piece == piece) return &socket;
        if (socket.piece.empty()) fileLevel = &socket;
    }
    return fileLevel;
}

inline const LodGroup* findLodGroup(const SceneBundle& bundle, std::string_view piece)
{
    for (const LodGroup& group : bundle.lodGroups) {
        if (group.piece == piece) return &group;
    }
    return nullptr;
}

// The mesh to draw `group` with at `distance`: the last level whose distance has been reached.
inline std::uint32_t lodMeshAt(const LodGroup& group, float distance)
{
    std::uint32_t mesh = group.levels.front().mesh;
    for (const LodLevel& level : group.levels) {
        if (distance >= level.distance) mesh = level.mesh;
    }
    return mesh;
}

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
