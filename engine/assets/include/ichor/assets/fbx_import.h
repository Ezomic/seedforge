#pragma once

#include "ichor/assets/bundle.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Imports a skinned FBX character (a rig, its skinned mesh, armature-only clips
// and rigid props) into the cooked bundle. Everything comes out in Ichor's frame:
// metres, Y up, forward -Z, feet on y = 0, so nothing at runtime knows the source
// was an FBX. docs/fbx-import.md has the reasoning behind each conversion.

struct FbxImportOptions {
    // The files are Y up with the character facing +Z (what Blender writes for a glTF
    // character). Ichor characters face -Z, so the whole model turns by this yaw.
    float yawDegrees = 180.0f;
    // The Exile clips carry root travel that runs against the way the legs plant their
    // feet. With this on, the travel keeps the direction the clip authored while the
    // body (turned by yawDegrees) walks the way its legs say; with it off the feet slide.
    bool reverseRootTravel = true;
    // Lift the model so the lowest vertex of the skinned mesh sits on y = 0.
    bool groundFeet = true;
};

// Where a prop attaches: the skinning matrix of `joint` moves a mesh that was baked
// at its bind placement, and `local` is applied before it.
struct JointSocket {
    std::string name;
    std::uint32_t joint = 0;
    std::uint32_t mesh = 0;
    glm::mat4 local{1.0f};
};

struct FbxRig {
    std::uint32_t skeleton = kNoSkeleton;
    std::uint32_t characterMesh = 0;
    glm::dmat4 modelFromFile{1.0}; // the axis, scale and ground conversion
    glm::dvec3 bindRoot{0.0}; // the root bone at bind, in model space
    std::vector<glm::mat4> bindWorld; // per joint, model space
    std::vector<std::string> jointNames;
    std::vector<std::int32_t> jointParent; // per joint, -1 for the root joint
    std::uint32_t unweightedVertices = 0;
};

// Appends the skeleton and the skinned mesh. The skeleton gets a root joint named
// "root" above the file's root bone: it carries the horizontal travel of the clips,
// which is what AnimationSet::rootTranslation reads.
bool importFbxCharacter(const std::filesystem::path& file, const FbxImportOptions& options, SceneBundle& bundle, FbxRig& rig);

// Appends one clip, resampled at 60 Hz, for the rig's skeleton.
bool importFbxClip(const std::filesystem::path& file, std::string_view clipName, const FbxImportOptions& options,
    const FbxRig& rig, SceneBundle& bundle);

// The model-space matrix of every joint at the first frame of a clip exactly as the file
// authors it, before the root bone is moved to the bind position. Props that were modelled
// in a hand sit where that pose says. Index order is the skeleton's.
bool importFbxClipFirstPose(const std::filesystem::path& file, const FbxRig& rig, const Skeleton& skeleton,
    std::vector<glm::mat4>& worlds);

// Appends a rigid mesh baked at the placement it has in its file, and returns its index.
bool importFbxProp(const std::filesystem::path& file, const FbxRig& rig, SceneBundle& bundle, std::uint32_t& meshIndex);

// Whichever of the named joints has its origin nearest the centre of a mesh in `jointWorlds`
// (one matrix per joint of the rig). kNoJoint when none of the names is a joint of the rig.
constexpr std::uint32_t kNoJoint = 0xffffffffu;
std::uint32_t nearestJoint(const FbxRig& rig, const std::vector<glm::mat4>& jointWorlds, const Mesh& mesh,
    std::initializer_list<std::string_view> jointNames);

}
