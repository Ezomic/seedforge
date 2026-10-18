#include "ichor/assets/exile.h"

#include "ichor/core/log.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <string>

namespace ichor {

namespace {

// The props were modelled in the hands of the Idle clip's first frame. The socket keeps that
// relation: baked at the file's placement, a prop moves with Hc(t) * inverse(Hc(0)), where Hc is the
// hand's authored world, so the joint's skinning matrix is followed by the bind-to-authored step.
bool addProp(const std::filesystem::path& file, const char* socketName, const std::vector<glm::mat4>& authoredPose,
    SceneBundle& bundle, ExileKit& kit)
{
    std::uint32_t mesh = 0;
    if (!importFbxProp(file, kit.rig, bundle, mesh)) return false;
    const std::uint32_t joint = nearestJoint(kit.rig, authoredPose, bundle.meshes[mesh], {"mixamorig:LeftHand", "mixamorig:RightHand"});
    if (joint == kNoJoint) {
        logf(LogLevel::Error, "{}: the rig has no hand to hold it", file.string());
        return false;
    }
    JointSocket socket;
    socket.name = socketName;
    socket.joint = joint;
    socket.mesh = mesh;
    socket.local = kit.rig.bindWorld[joint] * glm::inverse(authoredPose[joint]);
    kit.sockets.push_back(std::move(socket));
    return true;
}

}

bool cookExile(const std::filesystem::path& folder, const FbxImportOptions& options, SceneBundle& bundle, ExileKit& kit)
{
    kit = ExileKit{};
    if (!importFbxCharacter(folder / "SK_Exile.fbx", options, bundle, kit.rig)) return false;
    for (const char* clip : kExileClipNames) {
        if (!importFbxClip(folder / ("SK_Exile_" + std::string(clip) + ".fbx"), clip, options, kit.rig, bundle)) return false;
    }
    std::vector<glm::mat4> authored;
    if (!importFbxClipFirstPose(folder / "SK_Exile_Idle.fbx", kit.rig, bundle.skeletons[kit.rig.skeleton], authored)) return false;
    return addProp(folder / "SM_ExileSpear.fbx", "spear", authored, bundle, kit)
        && addProp(folder / "SM_ExileRoundShield.fbx", "shield", authored, bundle, kit);
}

}
