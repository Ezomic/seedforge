#include "ichor/stage0/grey_box_mesh.h"

#include "ichor/stage0/character_pose.h"

#include <cmath>

namespace ichor::stage0 {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr std::uint32_t kSegments = 16;
constexpr std::uint32_t kCapRings = 6;

struct ProfilePoint {
    float r;
    float y;
    float nr; // outward normal, radial part
    float ny;
};

Mesh revolve(const char* name, const std::vector<ProfilePoint>& profile, std::uint32_t material)
{
    Mesh mesh;
    mesh.name = name;
    const std::uint32_t rows = static_cast<std::uint32_t>(profile.size());
    for (std::uint32_t i = 0; i < rows; ++i) {
        for (std::uint32_t j = 0; j <= kSegments; ++j) {
            const float theta = kTwoPi * static_cast<float>(j) / static_cast<float>(kSegments);
            const float c = std::cos(theta);
            const float s = std::sin(theta);
            Vertex v{};
            v.position = {profile[i].r * c, profile[i].y, profile[i].r * s};
            v.normal = {profile[i].nr * c, profile[i].ny, profile[i].nr * s};
            v.tangent = {-s, 0.0f, c, 1.0f};
            v.uv = {static_cast<float>(j) / static_cast<float>(kSegments), static_cast<float>(i) / static_cast<float>(rows - 1)};
            v.weights[0] = 65535;
            mesh.vertices.push_back(v);
        }
    }
    const std::uint32_t stride = kSegments + 1;
    for (std::uint32_t i = 0; i + 1 < rows; ++i) {
        for (std::uint32_t j = 0; j < kSegments; ++j) {
            const std::uint32_t a = i * stride + j;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + stride;
            const std::uint32_t d = c + 1;
            // Theta runs against the profile's handedness, so the outward-facing winding is a, c, b.
            for (const std::uint32_t index : {a, c, b, b, c, d}) {
                mesh.indices.push_back(index);
            }
        }
    }
    mesh.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), material});
    mesh.boundsMin = mesh.boundsMax = mesh.vertices.front().position;
    for (const Vertex& v : mesh.vertices) {
        mesh.boundsMin = glm::min(mesh.boundsMin, v.position);
        mesh.boundsMax = glm::max(mesh.boundsMax, v.position);
    }
    return mesh;
}

}

Mesh capsuleMesh(float radius, float cylinderHalfHeight, std::uint32_t material)
{
    const float r = radius;
    const float h = cylinderHalfHeight;
    std::vector<ProfilePoint> profile;
    // The foot is the origin: the lower pole touches y = 0, the cylinder is centred at r + h.
    for (std::uint32_t i = 0; i <= kCapRings; ++i) {
        const float a = (kTwoPi / 4.0f) * static_cast<float>(i) / static_cast<float>(kCapRings);
        profile.push_back({r * std::sin(a), r - r * std::cos(a), std::sin(a), -std::cos(a)});
    }
    for (std::uint32_t i = 0; i <= kCapRings; ++i) {
        const float a = (kTwoPi / 4.0f) * static_cast<float>(i) / static_cast<float>(kCapRings);
        profile.push_back({r * std::cos(a), r + 2.0f * h + r * std::sin(a), std::cos(a), std::sin(a)});
    }
    return revolve("grey_capsule", profile, material);
}

Mesh spearMesh(std::uint32_t material)
{
    constexpr float shaft = 0.02f;
    constexpr float head = 0.055f;
    // The leaf head is a cone: its slope normal leans up by the ratio of head radius to length.
    const float slope = std::sqrt(0.3f * 0.3f + head * head);
    const float nr = 0.3f / slope;
    const float ny = head / slope;
    const std::vector<ProfilePoint> profile{
        {0.0f, -0.6f, 0.0f, -1.0f},
        {shaft, -0.6f, 0.0f, -1.0f},
        {shaft, -0.6f, 1.0f, 0.0f},
        {shaft, 1.1f, 1.0f, 0.0f},
        {shaft, 1.1f, 0.0f, -1.0f},
        {head, 1.1f, 0.0f, -1.0f},
        {head, 1.1f, nr, ny},
        {0.0f, 1.4f, nr, ny},
    };
    return revolve("grey_spear", profile, material);
}

Mesh shieldMesh(std::uint32_t material)
{
    constexpr float radius = 0.4f;
    constexpr float half = 0.03f;
    const std::vector<ProfilePoint> profile{
        {0.0f, -half, 0.0f, -1.0f},
        {radius, -half, 0.0f, -1.0f},
        {radius, -half, 1.0f, 0.0f},
        {radius, half, 1.0f, 0.0f},
        {radius, half, 0.0f, 1.0f},
        {0.0f, half, 0.0f, 1.0f},
    };
    return revolve("grey_shield", profile, material);
}

GreyBoxMeshes appendGreyBoxMeshes(SceneBundle& bundle)
{
    Material grey;
    grey.name = "grey_box";
    grey.baseColorFactor = {0.55f, 0.55f, 0.55f, 1.0f};
    grey.metallicFactor = 0.0f;
    grey.roughnessFactor = 0.9f;
    const std::uint32_t material = static_cast<std::uint32_t>(bundle.materials.size());
    bundle.materials.push_back(grey);

    GreyBoxMeshes out;
    out.body = static_cast<std::uint32_t>(bundle.meshes.size());
    bundle.meshes.push_back(capsuleMesh(kBodyRadius, 0.6f, material));
    out.spear = static_cast<std::uint32_t>(bundle.meshes.size());
    bundle.meshes.push_back(spearMesh(material));
    out.shield = static_cast<std::uint32_t>(bundle.meshes.size());
    bundle.meshes.push_back(shieldMesh(material));
    return out;
}

}
