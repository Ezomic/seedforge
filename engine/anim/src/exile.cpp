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

// The source clips straighten the elbows past straight (Idle, Block, Hit, by up to 42 degrees) and take the forearm up to
// 146 degrees round against the upper arm (Thrust, Swing, Death, Run). Each arm is solved again per sample as a two-bone
// chain: the shoulder and the hand stay where the source puts them, the elbow goes where a pole (outward, a little back
// and down, from the body) says, and it only hinges. The turn the source puts in the forearm moves to the wrist, and
// is saturated there. docs/exile-import.md has the measurements.
constexpr float kElbowFlexMinDegrees = 14.0f;  // never locked straight: stands the elbow out from the ribs
constexpr float kElbowFlexMaxDegrees = 150.0f;
constexpr float kWristTwistDegrees = 45.0f;
constexpr float kPoleBack = 0.4f;
constexpr float kPoleDown = 0.25f;
// How much of the source's own elbow plane (its upper arm roll) the plane keeps, the rest being the pole's.
constexpr float kRollKept = 0.7f;
// Idle, Walk and Run put the hands across the chest or flared out. Their hands are moved to hang beside the thigh with
// the palm to it, keeping the source's own motion: the hand's path is offset from the clip's mean, not replaced.
// `hang` is the reach used of the arm's length, `out` how far outward of the shoulder line the hand hangs.
struct Relaxed {
    const char* clip;
    float hang;
};
constexpr Relaxed kRelaxed[] = {{"idle", 0.93f}, {"walk", 0.93f}, {"run", 0.8f}};
constexpr float kRelaxedOut = 0.05f;

struct ArmChain {
    std::size_t arm = 0;
    std::size_t fore = 0;
    std::size_t hand = 0;
    float upperLength = 0.0f;
    float foreLength = 0.0f;
    float side = 1.0f;         // +1 for the left arm, -1 for the right
    glm::vec3 armAxis{0.0f};   // upper arm bone, in the upper arm's frame
    glm::vec3 foreAxis{0.0f};  // forearm bone, in the forearm's frame
    glm::vec3 armHinge{0.0f};  // the elbow's hinge axis, in the upper arm's frame
    glm::vec3 foreHinge{0.0f}; // and in the forearm's frame
    glm::quat handRest{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 handFinger{0.0f}; // the hand's finger and palm directions, in the hand's frame
    glm::vec3 handPalm{0.0f};
    glm::vec3 hangAt{0.0f};     // where a relaxed hand hangs, and its mean source position, in the rest pose's axes
    glm::vec3 meanAt{0.0f};
    bool relaxed = false;
};

struct BodyAxes {
    glm::quat hipsRest{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 hipsAt{0.0f};
    glm::vec3 lateral{0.0f}; // rest pose: left, up, forward
    glm::vec3 up{0.0f};
    glm::vec3 forward{0.0f};
};

struct ArmState {
    float twist = 0.0f;
    glm::vec3 normal{0.0f};
};

struct BodyJoints {
    std::size_t hips = 0;
    std::size_t neck = 0;
    std::size_t leftArm = 0;
    std::size_t rightArm = 0;
};

bool findJoint(const ClipFrames& clip, const std::string& name, std::size_t& out)
{
    const auto it = std::find(clip.jointNames.begin(), clip.jointNames.end(), name);
    out = static_cast<std::size_t>(it - clip.jointNames.begin());
    return it != clip.jointNames.end();
}

bool findArm(const ClipFrames& clip, const std::vector<glm::mat4>& restWorld, const std::string& side, glm::vec3 forward, ArmChain& chain)
{
    if (!findJoint(clip, "mixamorig:" + side + "Arm", chain.arm) || !findJoint(clip, "mixamorig:" + side + "ForeArm", chain.fore)
        || !findJoint(clip, "mixamorig:" + side + "Hand", chain.hand)) {
        return false;
    }
    chain.side = side == "Left" ? 1.0f : -1.0f;
    chain.upperLength = glm::length(clip.rest[chain.fore].translation);
    chain.foreLength = glm::length(clip.rest[chain.hand].translation);
    chain.armAxis = glm::normalize(clip.rest[chain.fore].translation);
    chain.foreAxis = glm::normalize(clip.rest[chain.hand].translation);
    const glm::mat3 axes(restWorld[chain.arm]);
    const glm::quat rest = glm::quat_cast(glm::mat3(glm::normalize(axes[0]), glm::normalize(axes[1]), glm::normalize(axes[2])));
    const glm::vec3 front = glm::inverse(rest) * forward;
    const glm::vec3 square = glm::normalize(front - chain.armAxis * glm::dot(front, chain.armAxis));
    chain.armHinge = glm::normalize(glm::cross(chain.armAxis, square));
    const glm::vec3 hinge = glm::inverse(clip.rest[chain.fore].rotation) * chain.armHinge;
    chain.foreHinge = glm::normalize(hinge - chain.foreAxis * glm::dot(hinge, chain.foreAxis));
    chain.handRest = clip.rest[chain.hand].rotation;
    std::size_t index = 0;
    std::size_t thumb = 0;
    if (findJoint(clip, "mixamorig:" + side + "HandIndex1", index) && findJoint(clip, "mixamorig:" + side + "HandThumb1", thumb)) {
        chain.handFinger = glm::normalize(clip.rest[index].translation);
        const glm::vec3 across = clip.rest[thumb].translation - chain.handFinger * glm::dot(clip.rest[thumb].translation, chain.handFinger);
        const glm::vec3 palm = glm::normalize(glm::cross(chain.handFinger, across));
        chain.handPalm = palm;
    }
    return true;
}

glm::quat framed(glm::vec3 along, glm::vec3 hinge, glm::vec3 localAlong, glm::vec3 localHinge)
{
    const glm::mat3 world(along, hinge, glm::cross(along, hinge));
    const glm::mat3 local(localAlong, localHinge, glm::cross(localAlong, localHinge));
    return glm::normalize(glm::quat_cast(world * glm::transpose(local)));
}

struct WorldPose {
    std::vector<glm::quat> rotation;
    std::vector<glm::vec3> position;
};

WorldPose worldOf(const ClipFrames& clip, const std::vector<JointPose>& pose)
{
    WorldPose world{std::vector<glm::quat>(pose.size()), std::vector<glm::vec3>(pose.size())};
    for (std::size_t j = 0; j < pose.size(); ++j) {
        const std::int32_t parent = clip.parents[j];
        if (parent < 0) {
            world.rotation[j] = pose[j].rotation;
            world.position[j] = pose[j].translation;
            continue;
        }
        const auto p = static_cast<std::size_t>(parent);
        world.rotation[j] = world.rotation[p] * pose[j].rotation;
        world.position[j] = world.position[p] + world.rotation[p] * (pose[j].translation * pose[p].scale);
    }
    return world;
}

glm::quat keepSide(glm::quat q, glm::quat previous)
{
    return glm::dot(q, previous) < 0.0f ? -q : q;
}

glm::vec3 across(glm::vec3 v, glm::vec3 axis)
{
    return v - axis * glm::dot(v, axis);
}

// Points in the rest pose's axes, the hips' motion taken out.
glm::vec3 inRestAxes(const BodyAxes& axes, const WorldPose& world, const BodyJoints& body, glm::vec3 point)
{
    const glm::quat relative = world.rotation[body.hips] * glm::inverse(axes.hipsRest);
    return glm::inverse(relative) * (point - world.position[body.hips]) + axes.hipsAt;
}

glm::vec3 fromRestAxes(const BodyAxes& axes, const WorldPose& world, const BodyJoints& body, glm::vec3 point)
{
    const glm::quat relative = world.rotation[body.hips] * glm::inverse(axes.hipsRest);
    return world.position[body.hips] + relative * (point - axes.hipsAt);
}

// `state` carries the arm's unwrapped wrist twist and elbow plane from the previous sample, so neither wraps or flips.
void solveArm(const ArmChain& chain, const BodyAxes& axes, const BodyJoints& body, const ClipFrames& clip, std::vector<JointPose>& pose,
    ArmState& state, bool first)
{
    const WorldPose world = worldOf(clip, pose);
    const glm::vec3 lateral = glm::normalize(world.position[body.leftArm] - world.position[body.rightArm]);
    const glm::vec3 up = glm::normalize(world.position[body.neck] - world.position[body.hips]);
    const glm::vec3 forward = glm::normalize(glm::cross(lateral, up));
    const glm::vec3 pole = chain.side * lateral - forward * kPoleBack - up * kPoleDown;

    const glm::vec3 shoulder = world.position[chain.arm];
    glm::vec3 wrist = world.position[chain.hand];
    if (chain.relaxed) {
        const glm::vec3 at = inRestAxes(axes, world, body, wrist);
        wrist = fromRestAxes(axes, world, body, chain.hangAt + (at - chain.meanAt));
    }
    const glm::vec3 toWrist = wrist - shoulder;
    const float l1 = chain.upperLength;
    const float l2 = chain.foreLength;
    const auto reach = [&](float flexDegrees) {
        return std::sqrt(std::max(0.0f, l1 * l1 + l2 * l2 + 2.0f * l1 * l2 * std::cos(glm::radians(flexDegrees))));
    };
    const float distance = glm::length(toWrist);
    if (!(distance > 1e-6f)) return;
    const glm::vec3 s = toWrist / distance;
    const float d = std::clamp(distance, reach(kElbowFlexMaxDegrees), reach(kElbowFlexMinDegrees));
    glm::vec3 q = across(pole, s);
    if (!(glm::length(q) > 1e-4f)) q = across(-forward, s);
    q = glm::normalize(q);
    const glm::vec3 polePlane = glm::normalize(glm::cross(q, s));

    // The elbow plane keeps most of the source's own: its upper arm's hinge, square to the new shoulder-hand line.
    glm::vec3 plane = polePlane;
    const glm::vec3 sourcePlane = across(world.rotation[chain.arm] * chain.armHinge, s);
    if (glm::length(sourcePlane) > 1e-3f) {
        glm::vec3 kept = glm::normalize(sourcePlane);
        if (glm::dot(kept, first ? polePlane : state.normal) < 0.0f) kept = -kept;
        state.normal = kept;
        const glm::vec3 blended = glm::mix(polePlane, kept, kRollKept);
        if (glm::length(blended) > 1e-3f) plane = glm::normalize(blended);
    }
    q = glm::cross(s, plane);

    const float along = (l1 * l1 - l2 * l2 + d * d) / (2.0f * d);
    const float out = std::sqrt(std::max(0.0f, l1 * l1 - along * along));
    const glm::vec3 elbow = shoulder + s * along + q * out;
    const glm::vec3 u = (elbow - shoulder) / l1;
    const glm::vec3 v = glm::normalize(shoulder + s * d - elbow);

    const glm::quat armWorld = framed(u, plane, chain.armAxis, chain.armHinge);
    const glm::quat foreWorld = framed(v, plane, chain.foreAxis, chain.foreHinge);
    const std::int32_t parent = clip.parents[chain.arm];
    const glm::quat parentWorld = parent < 0 ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : world.rotation[static_cast<std::size_t>(parent)];

    JointPose& arm = pose[chain.arm];
    JointPose& fore = pose[chain.fore];
    JointPose& hand = pose[chain.hand];
    arm.rotation = keepSide(glm::normalize(glm::inverse(parentWorld) * armWorld), arm.rotation);
    fore.rotation = keepSide(glm::normalize(glm::inverse(armWorld) * foreWorld), fore.rotation);

    glm::quat handWorld = world.rotation[chain.hand];
    if (chain.relaxed) {
        const glm::vec3 palm = across(-chain.side * lateral, v);
        if (glm::length(palm) > 1e-3f) handWorld = framed(v, glm::normalize(palm), chain.handFinger, chain.handPalm);
    }
    const glm::quat swungHand = glm::inverse(foreWorld) * handWorld;
    const glm::quat delta = swungHand * glm::inverse(chain.handRest);
    const float raw = 2.0f * std::atan2(glm::dot(glm::vec3(delta.x, delta.y, delta.z), chain.foreAxis), delta.w);
    const float twist = first ? std::remainder(raw, glm::two_pi<float>()) : raw + glm::two_pi<float>() * std::round((state.twist - raw) / glm::two_pi<float>());
    state.twist = twist;
    const float limit = glm::radians(kWristTwistDegrees);
    const float saturated = limit * std::tanh(twist / limit);
    hand.rotation = keepSide(glm::normalize(delta * glm::angleAxis(saturated - twist, chain.foreAxis) * chain.handRest), hand.rotation);
}

void solveArms(ClipFrames& clip)
{
    std::vector<glm::mat4> restWorld(clip.rest.size());
    for (std::size_t j = 0; j < clip.rest.size(); ++j) {
        const JointPose& p = clip.rest[j];
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), p.translation) * glm::mat4_cast(p.rotation) * glm::scale(glm::mat4(1.0f), p.scale);
        restWorld[j] = clip.parents[j] < 0 ? local : restWorld[static_cast<std::size_t>(clip.parents[j])] * local;
    }
    glm::vec3 at[4];
    const char* names[] = {"mixamorig:Spine", "mixamorig:Hips", "mixamorig:LeftUpLeg", "mixamorig:RightUpLeg"};
    for (int i = 0; i < 4; ++i) {
        std::size_t j = 0;
        if (!findJoint(clip, names[i], j)) return;
        at[i] = glm::vec3(restWorld[j][3]);
    }
    BodyJoints body;
    if (!findJoint(clip, "mixamorig:Hips", body.hips) || !findJoint(clip, "mixamorig:Neck", body.neck) || !findJoint(clip, "mixamorig:LeftArm", body.leftArm)
        || !findJoint(clip, "mixamorig:RightArm", body.rightArm)) {
        return;
    }
    BodyAxes axes;
    axes.up = glm::normalize(at[0] - at[1]);
    axes.lateral = glm::normalize(at[2] - at[3]);
    if (glm::length(glm::cross(axes.lateral, axes.up)) < 1e-6f) return;
    axes.forward = glm::normalize(glm::cross(axes.lateral, axes.up));
    axes.hipsAt = at[1];
    const glm::mat3 hipsAxes(restWorld[body.hips]);
    axes.hipsRest = glm::quat_cast(glm::mat3(glm::normalize(hipsAxes[0]), glm::normalize(hipsAxes[1]), glm::normalize(hipsAxes[2])));
    float hang = 0.0f;
    for (const Relaxed& r : kRelaxed) {
        if (clip.name == r.clip) hang = r.hang;
    }
    for (const char* side : {"Left", "Right"}) {
        ArmChain chain;
        if (!findArm(clip, restWorld, side, axes.forward, chain)) continue;
        if (chain.handFinger != glm::vec3(0.0f)) {
            const glm::mat3 handAxes(restWorld[chain.hand]);
            const glm::quat handRestWorld = glm::quat_cast(glm::mat3(glm::normalize(handAxes[0]), glm::normalize(handAxes[1]), glm::normalize(handAxes[2])));
            const glm::vec3 toward = -axes.up - chain.side * axes.lateral;
            if (glm::dot(handRestWorld * chain.handPalm, toward) < 0.0f) chain.handPalm = -chain.handPalm;
            chain.handPalm = glm::normalize(across(chain.handPalm, chain.handFinger));
        }
        chain.relaxed = hang > 0.0f && chain.handFinger != glm::vec3(0.0f);
        if (chain.relaxed) {
            const glm::vec3 shoulder(restWorld[chain.arm][3]);
            chain.hangAt = shoulder + chain.side * axes.lateral * kRelaxedOut - axes.up * (hang * (chain.upperLength + chain.foreLength));
            for (const std::vector<JointPose>& frame : clip.frames) {
                const WorldPose world = worldOf(clip, frame);
                chain.meanAt += inRestAxes(axes, world, body, world.position[chain.hand]);
            }
            chain.meanAt /= static_cast<float>(clip.frames.size());
        }
        ArmState state;
        for (std::size_t f = 0; f < clip.frames.size(); ++f) solveArm(chain, axes, body, clip, clip.frames[f], state, f == 0);
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
    options.clipPass = solveArms;
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
