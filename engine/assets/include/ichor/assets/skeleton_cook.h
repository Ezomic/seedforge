#pragma once

#include "ichor/assets/bundle.h"

#include <cstdint>
#include <vector>

struct cgltf_data;
struct cgltf_skin;
struct cgltf_animation;

namespace ichor {

// Builds an ozz skeleton from a glTF skin. `jointRemap[slot]` gives the ozz
// joint index for glTF joint slot `slot`, for remapping JOINTS_0.
// `rootYawDegrees` turns every root joint about the vertical, in the skeleton and in the clips, and leaves
// its translation (the root motion) as authored. Characters that were exported facing +Z use 180.
bool cookSkeleton(const cgltf_data& data, const cgltf_skin& skin, Skeleton& out, std::vector<std::uint16_t>& jointRemap,
    float rootYawDegrees = 0.0f);

// Samples a glTF animation for the skin's joints and builds an ozz
// animation. Returns false when the animation touches none of the joints.
bool cookClip(const cgltf_data& data, const cgltf_animation& animation, const cgltf_skin& skin, const Skeleton& skeleton,
    std::uint32_t skeletonIndex, AnimationClip& out, float rootYawDegrees = 0.0f);

}
