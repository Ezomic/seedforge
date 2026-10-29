#pragma once

#include "ichor/assets/bundle.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct cgltf_data;
struct cgltf_skin;
struct cgltf_animation;

namespace ichor {

struct JointPose {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// A clip as the cooker samples it, before the root yaw and before ozz compresses it: `name` is the clip's, frames[f][joint] are local
// poses in the skeleton's joint order, `rest` the joints' node transforms (the bind pose) and `parents` -1 at a root.
struct ClipFrames {
    const std::string& name;
    const std::vector<std::string>& jointNames;
    const std::vector<std::int32_t>& parents;
    const std::vector<JointPose>& rest;
    std::vector<std::vector<JointPose>>& frames;
};

using ClipPass = std::function<void(ClipFrames&)>;

// Builds an ozz skeleton from a glTF skin. `jointRemap[slot]` gives the ozz
// joint index for glTF joint slot `slot`, for remapping JOINTS_0.
// `rootYawDegrees` turns every root joint about the vertical, in the skeleton and in the clips, and turns its
// translation (the root motion) with it, so travel along the way the body faces stays along it. Characters that
// were exported facing +Z use 180.
bool cookSkeleton(const cgltf_data& data, const cgltf_skin& skin, Skeleton& out, std::vector<std::uint16_t>& jointRemap,
    float rootYawDegrees = 0.0f);

// Samples a glTF animation for the skin's joints and builds an ozz
// animation. Returns false when the animation touches none of the joints. `pass`, when set, may edit the sampled frames.
bool cookClip(const cgltf_data& data, const cgltf_animation& animation, const cgltf_skin& skin, const Skeleton& skeleton,
    std::uint32_t skeletonIndex, AnimationClip& out, float rootYawDegrees = 0.0f, const ClipPass& pass = {});

}
