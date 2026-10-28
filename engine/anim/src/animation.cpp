#include "ichor/anim/animation.h"

#include "ichor/assets/bundle.h"
#include "ichor/core/log.h"

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/containers/vector.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>

#include <algorithm>
#include <cstring>

namespace ichor {

namespace {

template <class T>
bool deserialize(const std::vector<std::uint8_t>& bytes, T& object)
{
    ozz::io::MemoryStream stream;
    stream.Write(bytes.data(), bytes.size());
    stream.Seek(0, ozz::io::Stream::kSet);
    ozz::io::IArchive archive(&stream);
    if (!archive.TestTag<T>()) {
        return false;
    }
    archive >> object;
    return true;
}

glm::mat4 toGlm(const ozz::math::Float4x4& m)
{
    glm::mat4 out;
    for (int c = 0; c < 4; ++c) {
        float column[4];
        ozz::math::StorePtrU(m.cols[c], column);
        out[c] = glm::vec4(column[0], column[1], column[2], column[3]);
    }
    return out;
}

}

struct AnimationSet::Impl {
    struct SkeletonEntry {
        ozz::animation::Skeleton skeleton;
        std::vector<std::string> jointNames;
        std::vector<glm::mat4> inverseBind;
    };
    struct ClipEntry {
        ozz::animation::Animation animation;
        std::string name;
        std::uint32_t skeleton = 0;
        float duration = 0.0f;
    };
    std::vector<SkeletonEntry> skeletons;
    std::vector<ClipEntry> clips;

    bool modelMatrices(std::uint32_t clip, float time, ozz::vector<ozz::math::Float4x4>& models) const
    {
        if (clip >= clips.size()) {
            return false;
        }
        const ClipEntry& entry = clips[clip];
        const SkeletonEntry& skel = skeletons[entry.skeleton];
        const int soaCount = skel.skeleton.num_soa_joints();

        ozz::vector<ozz::math::SoaTransform> locals(static_cast<std::size_t>(soaCount));
        ozz::animation::SamplingJob::Context context(skel.skeleton.num_joints());
        ozz::animation::SamplingJob sampling;
        sampling.animation = &entry.animation;
        sampling.context = &context;
        sampling.ratio = entry.duration > 0.0f ? std::clamp(time / entry.duration, 0.0f, 1.0f) : 0.0f;
        sampling.output = ozz::make_span(locals);
        if (!sampling.Run()) {
            return false;
        }

        models.resize(static_cast<std::size_t>(skel.skeleton.num_joints()));
        ozz::animation::LocalToModelJob toModel;
        toModel.skeleton = &skel.skeleton;
        toModel.input = ozz::make_span(locals);
        toModel.output = ozz::make_span(models);
        return toModel.Run();
    }
};

AnimationSet::AnimationSet()
    : impl_(std::make_unique<Impl>())
{
}

AnimationSet::~AnimationSet() = default;

void AnimationSet::clear()
{
    impl_->skeletons.clear();
    impl_->clips.clear();
}

bool AnimationSet::load(const SceneBundle& bundle)
{
    const auto skeletonBase = static_cast<std::uint32_t>(impl_->skeletons.size());
    for (const Skeleton& source : bundle.skeletons) {
        Impl::SkeletonEntry entry;
        if (!deserialize(source.ozzBytes, entry.skeleton)) {
            logf(LogLevel::Error, "skeleton {}: not an ozz skeleton", source.name);
            return false;
        }
        if (static_cast<std::size_t>(entry.skeleton.num_joints()) != source.jointNames.size()) {
            logf(LogLevel::Error, "skeleton {}: joint count disagrees with the bundle", source.name);
            return false;
        }
        entry.jointNames = source.jointNames;
        entry.inverseBind = source.inverseBind;
        impl_->skeletons.push_back(std::move(entry));
    }
    for (const AnimationClip& source : bundle.clips) {
        Impl::ClipEntry entry;
        const std::uint32_t skeleton = skeletonBase + source.skeleton;
        if (skeleton >= impl_->skeletons.size() || !deserialize(source.ozzBytes, entry.animation)) {
            logf(LogLevel::Error, "clip {}: not an ozz animation", source.name);
            return false;
        }
        if (entry.animation.num_tracks() != impl_->skeletons[skeleton].skeleton.num_joints()) {
            logf(LogLevel::Error, "clip {}: track count disagrees with its skeleton", source.name);
            return false;
        }
        entry.name = source.name;
        entry.skeleton = skeleton;
        entry.duration = source.duration;
        impl_->clips.push_back(std::move(entry));
    }
    return true;
}

std::uint32_t AnimationSet::skeletonCount() const { return static_cast<std::uint32_t>(impl_->skeletons.size()); }
std::uint32_t AnimationSet::clipCount() const { return static_cast<std::uint32_t>(impl_->clips.size()); }

std::uint32_t AnimationSet::jointCount(std::uint32_t skeleton) const
{
    return skeleton < impl_->skeletons.size() ? static_cast<std::uint32_t>(impl_->skeletons[skeleton].skeleton.num_joints()) : 0;
}

const std::vector<std::string>& AnimationSet::jointNames(std::uint32_t skeleton) const
{
    static const std::vector<std::string> kNone;
    return skeleton < impl_->skeletons.size() ? impl_->skeletons[skeleton].jointNames : kNone;
}

std::uint32_t AnimationSet::findClip(std::string_view name) const
{
    for (std::uint32_t i = 0; i < impl_->clips.size(); ++i) {
        if (impl_->clips[i].name == name) {
            return i;
        }
    }
    return kNoClip;
}

float AnimationSet::clipDuration(std::uint32_t clip) const
{
    return clip < impl_->clips.size() ? impl_->clips[clip].duration : 0.0f;
}

std::uint32_t AnimationSet::clipSkeleton(std::uint32_t clip) const
{
    return clip < impl_->clips.size() ? impl_->clips[clip].skeleton : 0;
}

bool AnimationSet::samplePose(std::uint32_t clip, float time, std::vector<glm::mat4>& out) const
{
    ozz::vector<ozz::math::Float4x4> models;
    if (!impl_->modelMatrices(clip, time, models)) {
        return false;
    }
    out.resize(models.size());
    for (std::size_t i = 0; i < models.size(); ++i) {
        out[i] = toGlm(models[i]);
    }
    return true;
}

bool AnimationSet::skinningMatrices(std::uint32_t clip, float time, std::vector<glm::mat4>& out) const
{
    if (!samplePose(clip, time, out)) {
        return false;
    }
    const auto& inverseBind = impl_->skeletons[impl_->clips[clip].skeleton].inverseBind;
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = out[i] * inverseBind[i];
    }
    return true;
}

bool AnimationSet::sample(std::uint32_t clip, float time, std::vector<glm::mat4>& skin, glm::vec3& rootOut) const
{
    ozz::vector<ozz::math::Float4x4> models;
    if (!impl_->modelMatrices(clip, time, models) || models.empty()) {
        return false;
    }
    const auto& inverseBind = impl_->skeletons[impl_->clips[clip].skeleton].inverseBind;
    skin.resize(models.size());
    for (std::size_t i = 0; i < models.size(); ++i) {
        const glm::mat4 model = toGlm(models[i]);
        if (i == 0) {
            rootOut = glm::vec3(model[3]);
        }
        skin[i] = model * inverseBind[i];
    }
    return true;
}

bool AnimationSet::rootTranslation(std::uint32_t clip, float time, glm::vec3& out) const
{
    ozz::vector<ozz::math::Float4x4> models;
    if (!impl_->modelMatrices(clip, time, models) || models.empty()) {
        return false;
    }
    const glm::mat4 root = toGlm(models[0]);
    out = glm::vec3(root[3]);
    return true;
}

void MontageState::start(const Montage& m)
{
    montage = m;
    time = 0.0f;
    phase = MontagePhase::Windup;
}

MontagePhase MontageState::advance(float dt)
{
    if (phase == MontagePhase::Idle) {
        return MontagePhase::Idle;
    }
    time += dt;
    // Windows are authored in seconds but land on ticks: compare with half a
    // tick of slack so 54 steps of 1/60 reach 0.9 whatever float rounding does.
    const float t = time + 0.5f * dt;
    MontagePhase next = phase;
    if (t >= montage.recoveryEnd) {
        next = MontagePhase::Idle;
    } else if (t >= montage.activeEnd) {
        next = MontagePhase::Recovery;
    } else if (t >= montage.activeStart) {
        next = MontagePhase::Active;
    }
    if (next == phase) {
        return MontagePhase::Idle;
    }
    phase = next;
    return next;
}

}
