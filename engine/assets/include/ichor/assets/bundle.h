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
};

struct Texture {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipCount = 0;
    TextureFormat format = TextureFormat::Rgba8Srgb;
    std::vector<std::uint8_t> pixels; // every mip level, tightly packed, largest first
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

}
