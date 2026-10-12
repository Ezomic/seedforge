#pragma once

#include "ichor/assets/bundle.h"

#include <filesystem>

namespace ichor {

struct CookStats {
    std::uint32_t verticesIn = 0;
    std::uint32_t verticesOut = 0;
    std::uint32_t triangles = 0;
    std::uint32_t tangentsGenerated = 0;
};

// glTF or GLB in, bundle out. Meshes are deduplicated and reordered for the
// vertex cache; textures get a full mip chain; node transforms are flattened.
bool cookGltf(const std::filesystem::path& source, SceneBundle& bundle, CookStats& stats);

}
