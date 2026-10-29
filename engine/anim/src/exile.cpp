#include "ichor/anim/exile.h"

#include "ichor/anim/animation.h"
#include "ichor/assets/cooker.h"
#include "ichor/core/log.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <string>

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

// The source clips of Idle, Block and Hit straighten the elbows past straight, by up to 42 degrees, and Thrust,
// Swing, Death and Run take the forearm up to 146 degrees round against the upper arm, further than a forearm turns.
// Samples past either limit are brought back to it, so a sample inside both is left exactly as cooked.
// docs/exile-import.md has the measurements.
constexpr float kElbowHyperextensionDegrees = 10.0f;
constexpr float kForearmTwistDegrees = 90.0f;

struct ArmChain {
    std::size_t arm = 0;
    std::size_t fore = 0;
    std::size_t hand = 0;
    glm::vec3 armAxis{0.0f};  // upper arm bone, in the upper arm's frame
    glm::vec3 foreAxis{0.0f}; // forearm bone, in the forearm's frame
    glm::vec3 front{0.0f};    // the body's forward at the bind pose, in the upper arm's frame, square to the bone
    glm::quat elbowRest{1.0f, 0.0f, 0.0f, 0.0f};
};

bool findArm(const ClipFrames& clip, const std::vector<glm::mat4>& restWorld, const std::string& side, glm::vec3 forward, ArmChain& chain)
{
    const auto find = [&](const std::string& name, std::size_t& out) {
        const auto it = std::find(clip.jointNames.begin(), clip.jointNames.end(), "mixamorig:" + side + name);
        out = static_cast<std::size_t>(it - clip.jointNames.begin());
        return it != clip.jointNames.end();
    };
    if (!find("Arm", chain.arm) || !find("ForeArm", chain.fore) || !find("Hand", chain.hand)) return false;
    chain.armAxis = glm::normalize(clip.rest[chain.fore].translation);
    chain.foreAxis = glm::normalize(clip.rest[chain.hand].translation);
    const glm::mat3 axes(restWorld[chain.arm]);
    const glm::quat rest = glm::quat_cast(glm::mat3(glm::normalize(axes[0]), glm::normalize(axes[1]), glm::normalize(axes[2])));
    const glm::vec3 front = glm::inverse(rest) * forward;
    chain.front = glm::normalize(front - chain.armAxis * glm::dot(front, chain.armAxis));
    chain.elbowRest = clip.rest[chain.fore].rotation;
    return true;
}

// The hand keeps its rotation against the upper arm, so a held prop keeps its angle and only moves with the wrist.
void limitElbow(const ArmChain& chain, std::vector<JointPose>& pose)
{
    glm::quat& fore = pose[chain.fore].rotation;
    glm::quat& hand = pose[chain.hand].rotation;
    const float hyperextension = glm::radians(kElbowHyperextensionDegrees);
    const float twistLimit = glm::radians(kForearmTwistDegrees);
    const glm::quat foreHand = fore * hand;
    bool changed = false;
    const glm::vec3 forearm = glm::normalize(fore * chain.foreAxis);
    const float behind = -glm::dot(forearm, chain.front);
    if (behind > std::sin(hyperextension)) {
        glm::vec3 side = forearm + chain.front * behind;
        side = glm::length(side) > 1e-6f ? glm::normalize(side) : chain.armAxis;
        const glm::vec3 limited = side * std::cos(hyperextension) - chain.front * std::sin(hyperextension);
        fore = glm::normalize(glm::quat(forearm, limited) * fore);
        changed = true;
    }
    const glm::quat d = glm::inverse(chain.elbowRest) * fore;
    const float twist = std::remainder(2.0f * std::atan2(glm::dot(glm::vec3(d.x, d.y, d.z), chain.foreAxis), d.w), glm::two_pi<float>());
    if (std::abs(twist) > twistLimit) {
        fore = glm::normalize(fore * glm::angleAxis(std::copysign(twistLimit, twist) - twist, chain.foreAxis));
        changed = true;
    }
    if (changed) hand = glm::normalize(glm::inverse(fore) * foreHand);
}

void limitElbows(ClipFrames& clip)
{
    std::vector<glm::mat4> restWorld(clip.rest.size());
    for (std::size_t j = 0; j < clip.rest.size(); ++j) {
        const JointPose& p = clip.rest[j];
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), p.translation) * glm::mat4_cast(p.rotation) * glm::scale(glm::mat4(1.0f), p.scale);
        restWorld[j] = clip.parents[j] < 0 ? local : restWorld[static_cast<std::size_t>(clip.parents[j])] * local;
    }
    glm::vec3 at[4];
    const char* body[] = {"mixamorig:Spine", "mixamorig:Hips", "mixamorig:LeftUpLeg", "mixamorig:RightUpLeg"};
    for (int i = 0; i < 4; ++i) {
        const auto it = std::find(clip.jointNames.begin(), clip.jointNames.end(), body[i]);
        if (it == clip.jointNames.end()) return;
        at[i] = glm::vec3(restWorld[static_cast<std::size_t>(it - clip.jointNames.begin())][3]);
    }
    const glm::vec3 up = at[0] - at[1];
    const glm::vec3 left = at[2] - at[3];
    if (glm::length(glm::cross(left, up)) < 1e-6f) return;
    const glm::vec3 forward = glm::normalize(glm::cross(left, up));
    for (const char* side : {"Left", "Right"}) {
        ArmChain chain;
        if (!findArm(clip, restWorld, side, forward, chain)) continue;
        for (std::vector<JointPose>& pose : clip.frames) limitElbow(chain, pose);
    }
}

}

bool cookExile(const std::filesystem::path& folder, ExileKit& kit)
{
    kit = ExileKit{};
    CookStats stats;
    // The character was exported facing +Z and Ichor's characters face -Z, so the root turns half a way round.
    CookOptions options;
    options.rootYawDegrees = 180.0f;
    options.clipPass = limitElbows;
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
    ExileBody& body = kit.body;
    const std::uint32_t leftUpLeg = jointNamed(skeleton, "mixamorig:LeftUpLeg");
    const std::uint32_t rightUpLeg = jointNamed(skeleton, "mixamorig:RightUpLeg");
    body.hips = jointNamed(skeleton, "mixamorig:Hips");
    body.spine = jointNamed(skeleton, "mixamorig:Spine");
    for (const std::uint32_t j : {leftUpLeg, rightUpLeg, body.hips, body.spine}) {
        if (j == kNoJointFound || j >= pose.size()) {
            logf(LogLevel::Error, "{}: the rig lacks the hips, spine or up-legs", folder.string());
            return false;
        }
    }
    const auto bindAt = [&](std::uint32_t j) { return glm::vec3(glm::inverse(skeleton.inverseBind[j])[3]); };
    body.up = glm::normalize(bindAt(body.spine) - bindAt(body.hips));
    body.forward = glm::normalize(glm::cross(bindAt(leftUpLeg) - bindAt(rightUpLeg), body.up));
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
        socket.facesForward = !prop.longProp;
        socket.held = heldOrientation(mesh, prop.longProp);
        kit.sockets.push_back(std::move(socket));
        meshBase += static_cast<std::uint32_t>(kit.bundles[prop.bundle].meshes.size());
    }
    return true;
}

glm::mat4 heldMatrix(const ExileKit& kit, const JointSocket& socket, const std::vector<glm::mat4>& skin)
{
    if (socket.joint >= skin.size()) return glm::mat4(1.0f);
    const glm::mat4 carried = skin[socket.joint] * socket.local;
    const ExileBody& body = kit.body;
    const auto& inverseBind = kit.bundles[0].skeletons[0].inverseBind;
    if (!socket.facesForward || body.hips >= skin.size() || body.spine >= skin.size() || socket.joint >= inverseBind.size()) return carried;
    // A skinning matrix takes bind-pose directions to posed ones.
    const glm::vec3 back = -glm::normalize(glm::mat3(skin[body.hips]) * body.forward);
    const glm::vec3 up = glm::mat3(skin[body.spine]) * body.up;
    const glm::vec3 square = up - back * glm::dot(up, back);
    if (!(glm::length(square) > 1e-6f)) return carried;
    const glm::vec3 y = glm::normalize(square);
    const glm::vec3 centre(skin[socket.joint] * glm::inverse(inverseBind[socket.joint])[3]);
    const glm::mat4 facing(glm::vec4(glm::cross(y, back), 0.0f), glm::vec4(y, 0.0f), glm::vec4(back, 0.0f), glm::vec4(centre, 1.0f));
    return facing * socket.held;
}

}
