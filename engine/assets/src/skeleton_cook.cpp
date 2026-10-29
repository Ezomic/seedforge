#include "ichor/assets/skeleton_cook.h"

#include "ichor/core/log.h"

#include <cgltf.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/soa_transform.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace ichor {

namespace {

constexpr float kSampleRate = 60.0f;

using Trs = JointPose;

Trs decomposeMatrix(const glm::mat4& m)
{
    Trs trs;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(m, trs.scale, trs.rotation, trs.translation, skew, perspective);
    return trs;
}

Trs localTransform(const cgltf_node& node)
{
    Trs trs;
    if (node.has_matrix) {
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(glm::make_mat4(node.matrix), trs.scale, trs.rotation, trs.translation, skew, perspective);
        return trs;
    }
    if (node.has_translation) trs.translation = glm::make_vec3(node.translation);
    if (node.has_rotation) trs.rotation = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
    if (node.has_scale) trs.scale = glm::make_vec3(node.scale);
    return trs;
}

ozz::math::Transform toOzz(const Trs& trs)
{
    ozz::math::Transform t;
    t.translation = {trs.translation.x, trs.translation.y, trs.translation.z};
    t.rotation = {trs.rotation.x, trs.rotation.y, trs.rotation.z, trs.rotation.w};
    t.scale = {trs.scale.x, trs.scale.y, trs.scale.z};
    return t;
}

glm::quat yawQuat(float degrees)
{
    return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
}

template <class T>
bool serialize(const T& object, std::vector<std::uint8_t>& out)
{
    ozz::io::MemoryStream stream;
    {
        ozz::io::OArchive archive(&stream);
        archive << object;
    }
    const auto size = static_cast<std::size_t>(stream.Size());
    out.resize(size);
    stream.Seek(0, ozz::io::Stream::kSet);
    return stream.Read(out.data(), size) == size;
}

// Linear or step interpolation of a glTF sampler at time t.
template <int N>
void sampleChannel(const cgltf_animation_sampler& sampler, float t, float out[N])
{
    const cgltf_accessor* times = sampler.input;
    const cgltf_accessor* values = sampler.output;
    const cgltf_size count = times->count;
    float t0 = 0.0f;
    float t1 = 0.0f;
    cgltf_size i = 0;
    cgltf_accessor_read_float(times, 0, &t0, 1);
    if (t <= t0 || count == 1) {
        cgltf_accessor_read_float(values, 0, out, N);
        return;
    }
    for (i = 1; i < count; ++i) {
        cgltf_accessor_read_float(times, i, &t1, 1);
        if (t < t1) {
            break;
        }
        t0 = t1;
    }
    if (i >= count) {
        cgltf_accessor_read_float(values, count - 1, out, N);
        return;
    }
    float a[N];
    float b[N];
    cgltf_accessor_read_float(values, i - 1, a, N);
    cgltf_accessor_read_float(values, i, b, N);
    if constexpr (N == 4) {
        // Quaternions on opposite hemispheres would swing the long way round.
        float dot = 0.0f;
        for (int k = 0; k < 4; ++k) dot += a[k] * b[k];
        if (dot < 0.0f) {
            for (int k = 0; k < 4; ++k) b[k] = -b[k];
        }
    }
    const float u = sampler.interpolation == cgltf_interpolation_type_step ? 0.0f : (t - t0) / std::max(t1 - t0, 1e-6f);
    for (int k = 0; k < N; ++k) {
        out[k] = a[k] + (b[k] - a[k]) * u;
    }
}

bool isJointOf(const cgltf_skin& skin, const cgltf_node* node)
{
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        if (skin.joints[j] == node) return true;
    }
    return false;
}

float lastKeyTime(const cgltf_animation_sampler& sampler)
{
    if (sampler.input->has_max) {
        return sampler.input->max[0];
    }
    float t = 0.0f;
    if (sampler.input->count > 0) {
        cgltf_accessor_read_float(sampler.input, sampler.input->count - 1, &t, 1);
    }
    return t;
}

}

bool cookSkeleton(const cgltf_data& data, const cgltf_skin& skin, Skeleton& out, std::vector<std::uint16_t>& jointRemap, float rootYawDegrees)
{
    out.name = skin.name != nullptr ? skin.name : "";
    std::unordered_map<const cgltf_node*, cgltf_size> slotOf;
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        slotOf[skin.joints[j]] = j;
    }

    // Roots are joints whose parent is not a joint of this skin.
    std::vector<const cgltf_node*> roots;
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        const cgltf_node* joint = skin.joints[j];
        if (joint->parent == nullptr || !slotOf.contains(joint->parent)) {
            roots.push_back(joint);
        }
    }
    if (roots.empty()) {
        logf(LogLevel::Error, "skin {}: no root joint", out.name);
        return false;
    }

    ozz::animation::offline::RawSkeleton raw;
    std::function<void(const cgltf_node*, ozz::animation::offline::RawSkeleton::Joint&)> build;
    build = [&](const cgltf_node* node, ozz::animation::offline::RawSkeleton::Joint& joint) {
        joint.name = node->name != nullptr ? node->name : "joint";
        joint.transform = toOzz(localTransform(*node));
        for (cgltf_size c = 0; c < node->children_count; ++c) {
            if (slotOf.contains(node->children[c])) {
                joint.children.emplace_back();
                build(node->children[c], joint.children.back());
            }
        }
    };
    for (const cgltf_node* root : roots) {
        raw.roots.emplace_back();
        build(root, raw.roots.back());
        // Inverse bind matrices are world-relative, so whatever sits above the
        // root joint (an armature node, a 0.01 scale) is folded into its local transform.
        if (root->parent != nullptr) {
            float parentWorld[16];
            cgltf_node_transform_world(root->parent, parentWorld);
            float local[16];
            cgltf_node_transform_local(root, local);
            raw.roots.back().transform = toOzz(decomposeMatrix(glm::make_mat4(parentWorld) * glm::make_mat4(local)));
        }
        if (rootYawDegrees != 0.0f) {
            auto& rotation = raw.roots.back().transform.rotation;
            const glm::quat turned = yawQuat(rootYawDegrees) * glm::quat(rotation.w, rotation.x, rotation.y, rotation.z);
            rotation = {turned.x, turned.y, turned.z, turned.w};
            auto& translation = raw.roots.back().transform.translation;
            const glm::vec3 moved = yawQuat(rootYawDegrees) * glm::vec3(translation.x, translation.y, translation.z);
            translation = {moved.x, moved.y, moved.z};
        }
    }
    if (!raw.Validate()) {
        logf(LogLevel::Error, "skin {}: invalid skeleton", out.name);
        return false;
    }

    ozz::animation::offline::SkeletonBuilder builder;
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton = builder(raw);
    if (!skeleton) {
        logf(LogLevel::Error, "skin {}: skeleton build failed", out.name);
        return false;
    }

    // ozz orders joints depth-first; map glTF slots onto that order by name.
    const auto names = skeleton->joint_names();
    std::unordered_map<std::string, std::uint16_t> indexOfName;
    for (int i = 0; i < skeleton->num_joints(); ++i) {
        out.jointNames.emplace_back(names[i]);
        indexOfName[names[i]] = static_cast<std::uint16_t>(i);
    }
    if (indexOfName.size() != static_cast<std::size_t>(skeleton->num_joints())) {
        logf(LogLevel::Error, "skin {}: joint names must be unique and present", out.name);
        return false;
    }
    if (skin.inverse_bind_matrices != nullptr && skin.inverse_bind_matrices->count < skin.joints_count) {
        logf(LogLevel::Error, "skin {}: fewer inverse bind matrices than joints", out.name);
        return false;
    }
    jointRemap.resize(skin.joints_count);
    out.inverseBind.assign(skeleton->num_joints(), glm::mat4(1.0f));
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        const cgltf_node* joint = skin.joints[j];
        const auto found = indexOfName.find(joint->name != nullptr ? joint->name : "joint");
        if (found == indexOfName.end()) {
            logf(LogLevel::Error, "skin {}: joint names must be unique", out.name);
            return false;
        }
        jointRemap[j] = found->second;
        if (skin.inverse_bind_matrices != nullptr) {
            float m[16];
            cgltf_accessor_read_float(skin.inverse_bind_matrices, j, m, 16);
            out.inverseBind[found->second] = glm::make_mat4(m);
        }
    }
    (void)data;
    return serialize(*skeleton, out.ozzBytes);
}

bool cookClip(const cgltf_data& data, const cgltf_animation& animation, const cgltf_skin& skin, const Skeleton& skeleton,
    std::uint32_t skeletonIndex, AnimationClip& out, float rootYawDegrees, const ClipPass& pass)
{
    (void)data;
    out.name = animation.name != nullptr ? animation.name : "clip";
    out.skeleton = skeletonIndex;

    // Which channels drive which joint, by ozz joint index.
    struct JointChannels {
        const cgltf_animation_sampler* translation = nullptr;
        const cgltf_animation_sampler* rotation = nullptr;
        const cgltf_animation_sampler* scale = nullptr;
    };
    std::unordered_map<std::string, std::size_t> jointIndex;
    for (std::size_t i = 0; i < skeleton.jointNames.size(); ++i) {
        jointIndex[skeleton.jointNames[i]] = i;
    }
    std::vector<JointChannels> channels(skeleton.jointNames.size());
    std::vector<const cgltf_node*> jointNodes(skeleton.jointNames.size(), nullptr);
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        const cgltf_node* node = skin.joints[j];
        const auto found = jointIndex.find(node->name != nullptr ? node->name : "joint");
        if (found != jointIndex.end()) {
            jointNodes[found->second] = node;
        }
    }
    float duration = 0.0f;
    bool any = false;
    for (cgltf_size c = 0; c < animation.channels_count; ++c) {
        const cgltf_animation_channel& channel = animation.channels[c];
        if (channel.target_node == nullptr || channel.target_node->name == nullptr) {
            continue;
        }
        const auto found = jointIndex.find(channel.target_node->name);
        if (found == jointIndex.end()) {
            continue;
        }
        if (channel.sampler->interpolation == cgltf_interpolation_type_cubic_spline) {
            logf(LogLevel::Error, "clip {}: cubic spline samplers are not supported, export with linear keys", out.name);
            return false;
        }
        any = true;
        JointChannels& jc = channels[found->second];
        if (channel.target_path == cgltf_animation_path_type_translation) jc.translation = channel.sampler;
        if (channel.target_path == cgltf_animation_path_type_rotation) jc.rotation = channel.sampler;
        if (channel.target_path == cgltf_animation_path_type_scale) jc.scale = channel.sampler;
        duration = std::max(duration, lastKeyTime(*channel.sampler));
    }
    if (!any || duration <= 0.0f) {
        return false;
    }

    // Sample every joint at a fixed rate; ozz compresses and compacts the keys.
    ozz::animation::offline::RawAnimation raw;
    raw.name = out.name;
    raw.duration = duration;
    raw.tracks.resize(skeleton.jointNames.size());
    const auto frames = static_cast<int>(std::ceil(duration * kSampleRate)) + 1;
    std::vector<Trs> rest(channels.size());
    std::vector<std::int32_t> parents(channels.size(), -1);
    std::vector<std::vector<Trs>> poses(static_cast<std::size_t>(frames), std::vector<Trs>(channels.size()));
    for (std::size_t j = 0; j < channels.size(); ++j) {
        if (jointNodes[j] != nullptr) {
            rest[j] = localTransform(*jointNodes[j]);
            const cgltf_node* parent = jointNodes[j]->parent;
            if (parent != nullptr && parent->name != nullptr && isJointOf(skin, parent)) parents[j] = static_cast<std::int32_t>(jointIndex.at(parent->name));
        }
        for (int f = 0; f < frames; ++f) {
            const float t = std::min(duration, static_cast<float>(f) / kSampleRate);
            Trs& trs = poses[static_cast<std::size_t>(f)][j];
            trs = rest[j];
            if (channels[j].translation) sampleChannel<3>(*channels[j].translation, t, &trs.translation.x);
            if (channels[j].rotation) {
                float q[4];
                sampleChannel<4>(*channels[j].rotation, t, q);
                trs.rotation = glm::normalize(glm::quat(q[3], q[0], q[1], q[2]));
            }
            if (channels[j].scale) sampleChannel<3>(*channels[j].scale, t, &trs.scale.x);
        }
    }
    if (pass) {
        ClipFrames clip{skeleton.jointNames, parents, rest, poses};
        pass(clip);
    }
    for (std::size_t j = 0; j < channels.size(); ++j) {
        const bool isRoot = jointNodes[j] != nullptr && (jointNodes[j]->parent == nullptr || !isJointOf(skin, jointNodes[j]->parent));
        auto& track = raw.tracks[j];
        for (int f = 0; f < frames; ++f) {
            const float t = std::min(duration, static_cast<float>(f) / kSampleRate);
            Trs trs = poses[static_cast<std::size_t>(f)][j];
            if (isRoot && rootYawDegrees != 0.0f) {
                trs.rotation = yawQuat(rootYawDegrees) * trs.rotation;
                trs.translation = yawQuat(rootYawDegrees) * trs.translation;
            }
            track.translations.push_back({t, {trs.translation.x, trs.translation.y, trs.translation.z}});
            track.rotations.push_back({t, {trs.rotation.x, trs.rotation.y, trs.rotation.z, trs.rotation.w}});
            track.scales.push_back({t, {trs.scale.x, trs.scale.y, trs.scale.z}});
        }
    }
    if (!raw.Validate()) {
        logf(LogLevel::Error, "clip {}: invalid animation", out.name);
        return false;
    }
    ozz::animation::offline::AnimationBuilder builder;
    ozz::unique_ptr<ozz::animation::Animation> built = builder(raw);
    if (!built) {
        logf(LogLevel::Error, "clip {}: animation build failed", out.name);
        return false;
    }
    out.duration = duration;
    return serialize(*built, out.ozzBytes);
}

}
