#pragma once

#include "ichor/assets/bundle.h"

#include <cstdint>

namespace ichor::stage0 {

// Procedural grey-box geometry, so Stage 0 needs no art assets. All of it is revolved from a
// profile about +Y, with outward normals and counter-clockwise front faces.
Mesh capsuleMesh(float radius, float cylinderHalfHeight, std::uint32_t material);
Mesh spearMesh(std::uint32_t material); // grip at the origin, butt at -0.6, tip at +1.4
Mesh shieldMesh(std::uint32_t material); // round disc, face normal +Y

struct GreyBoxMeshes {
    std::uint32_t body = 0;
    std::uint32_t spear = 0;
    std::uint32_t shield = 0;
};

// Adds one grey material and the three meshes to the bundle before it is uploaded.
GreyBoxMeshes appendGreyBoxMeshes(SceneBundle& bundle);

}
