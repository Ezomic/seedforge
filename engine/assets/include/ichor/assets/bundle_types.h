#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// The parts of a cooked scene bundle most of the engine builds or consumes: meshes, materials,
// skeletons, clips, sounds and nodes. These change rarely. Texture storage and SceneBundle itself
// live in bundle.h, so a change to how textures are stored rebuilds only the code that cooks,
// loads or uploads them. Include bundle.h only where a complete Texture or SceneBundle is needed.

struct Texture;
struct SceneBundle;

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

// A foot meeting the ground, found at cook time from the foot joints' heights.
struct FootContact {
    float time = 0.0f; // seconds from the clip start
    std::uint32_t foot = 0; // 0 left, 1 right
};

struct AnimationClip {
    std::string name;
    std::uint32_t skeleton = 0;
    float duration = 0.0f; // seconds
    std::vector<std::uint8_t> ozzBytes;
    std::vector<FootContact> footContacts; // by time; empty for a clip that does not walk
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

}
