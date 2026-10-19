#include "ichor/anim/exile.h"

#include "ichor/anim/animation.h"
#include "ichor/assets/cooker.h"
#include "ichor/core/log.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace ichor {

namespace {

constexpr std::uint32_t kNoJointFound = 0xffffffffu;

std::uint32_t jointNamed(const Skeleton& skeleton, std::string_view name)
{
    const auto it = std::find(skeleton.jointNames.begin(), skeleton.jointNames.end(), name);
    return it == skeleton.jointNames.end() ? kNoJointFound : static_cast<std::uint32_t>(it - skeleton.jointNames.begin());
}

// Turns the prop's own axes into the pose it is held in: the long axis of the spear upright, the
// flat face of the shield looking forward (-Z).
glm::mat4 heldOrientation(const Mesh& prop, bool longProp)
{
    const glm::vec3 extent = prop.boundsMax - prop.boundsMin;
    if (longProp) {
        if (extent.x > extent.y && extent.x > extent.z) return glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));
        if (extent.z > extent.y) return glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0));
        return glm::mat4(1.0f);
    }
    if (extent.x < extent.y && extent.x < extent.z) return glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
    if (extent.y < extent.z) return glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0));
    return glm::mat4(1.0f);
}

}

bool cookExile(const std::filesystem::path& folder, ExileKit& kit)
{
    kit = ExileKit{};
    CookStats stats;
    // The character was exported facing +Z and Ichor's characters face -Z, so the root turns half a way round.
    CookOptions options;
    options.rootYawDegrees = 180.0f;
    const char* files[] = {"SK_Exile.glb", "SM_ExileSpear.glb", "SM_ExileRoundShield.glb"};
    for (std::size_t i = 0; i < kit.bundles.size(); ++i) {
        if (!cookGltf(folder / files[i], kit.bundles[i], stats, options)) return false;
    }
    const SceneBundle& character = kit.bundles[0];
    if (character.skeletons.size() != 1 || character.meshes.empty() || kit.bundles[1].meshes.empty() || kit.bundles[2].meshes.empty()) {
        logf(LogLevel::Error, "{}: the Exile needs one skeleton and a mesh in each file", folder.string());
        return false;
    }
    AnimationSet set;
    if (!set.load(character)) return false;
    const std::uint32_t idle = set.findClip("idle");
    std::vector<glm::mat4> pose;
    if (idle == AnimationSet::kNoClip || !set.samplePose(idle, 0.0f, pose)) {
        logf(LogLevel::Error, "{}: the Exile has no idle clip", folder.string());
        return false;
    }

    // The props' origins sit at the grip and at the shield's centre. Each is put at its hand as
    // the hand stands at the first frame of idle, turned to the held pose; from there the hand
    // carries it: world = joint(t) * offset, and the joint's skinning matrix is joint(t) * inverse bind.
    const Skeleton& skeleton = character.skeletons[0];
    struct Prop {
        const char* name;
        const char* hand;
        std::size_t bundle;
        bool longProp;
    };
    std::uint32_t meshBase = static_cast<std::uint32_t>(character.meshes.size());
    for (const Prop& prop : {Prop{"spear", "mixamorig:RightHand", 1, true}, Prop{"shield", "mixamorig:LeftHand", 2, false}}) {
        const std::uint32_t joint = jointNamed(skeleton, prop.hand);
        if (joint == kNoJointFound || joint >= pose.size()) {
            logf(LogLevel::Error, "{}: the rig has no {}", folder.string(), prop.hand);
            return false;
        }
        const Mesh& mesh = kit.bundles[prop.bundle].meshes[0];
        const glm::mat4 gripAtIdle = glm::translate(glm::mat4(1.0f), glm::vec3(pose[joint][3])) * heldOrientation(mesh, prop.longProp);
        const glm::mat4 offset = glm::inverse(pose[joint]) * gripAtIdle;
        JointSocket socket;
        socket.name = prop.name;
        socket.joint = joint;
        socket.mesh = meshBase;
        socket.local = glm::inverse(skeleton.inverseBind[joint]) * offset;
        kit.sockets.push_back(std::move(socket));
        meshBase += static_cast<std::uint32_t>(kit.bundles[prop.bundle].meshes.size());
    }
    return true;
}

}
