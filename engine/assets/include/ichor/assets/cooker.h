#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/assets/piece_conventions.h"
#include "ichor/assets/skeleton_cook.h"

#include <filesystem>

namespace ichor {

struct CookStats {
    std::uint32_t verticesIn = 0;
    std::uint32_t verticesOut = 0;
    std::uint32_t triangles = 0;
    std::uint32_t tangentsGenerated = 0;
    std::uint32_t clips = 0;
};

struct CookOptions {
    float rootYawDegrees = 0.0f; // see cookSkeleton
    ClipPass clipPass; // see cookClip
};

// glTF or GLB in, bundle out. Meshes are deduplicated and reordered for the
// vertex cache; textures get a full mip chain; node transforms are flattened.
bool cookGltf(const std::filesystem::path& source, SceneBundle& bundle, CookStats& stats, const CookOptions& options = {});

// The same, also returning the sockets, LOD groups and collision shapes named in the file.
bool cookGltf(const std::filesystem::path& source, SceneBundle& bundle, PieceData& pieces, CookStats& stats, const CookOptions& options = {});

// The validator mode: what cookGltf would find in the names of `source`, without cooking meshes or textures.
bool inspectGltf(const std::filesystem::path& source, ConventionScan& scan);

}
