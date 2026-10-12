#pragma once

#include "ichor/assets/bundle.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace ichor {

// Skeletons and clips from a bundle, ready to sample. Sampling is pure: the
// same clip and time always give the same pose, so it belongs on the fixed
// step and never in the renderer.
class AnimationSet {
public:
    AnimationSet();
    ~AnimationSet();
    AnimationSet(const AnimationSet&) = delete;
    AnimationSet& operator=(const AnimationSet&) = delete;

    // Appends: the bundle's skeletons and clips get indices continuing from
    // skeletonCount() and clipCount() as they were before the call.
    bool load(const SceneBundle& bundle);
    void clear();

    std::uint32_t skeletonCount() const;
    std::uint32_t clipCount() const;
    std::uint32_t jointCount(std::uint32_t skeleton) const;
    const std::vector<std::string>& jointNames(std::uint32_t skeleton) const;
    std::uint32_t findClip(std::string_view name) const; // kNoClip when absent
    float clipDuration(std::uint32_t clip) const;
    std::uint32_t clipSkeleton(std::uint32_t clip) const;

    // Model-space joint matrices for the clip at `time` (seconds, clamped to
    // the clip). `out` is resized to the joint count.
    bool samplePose(std::uint32_t clip, float time, std::vector<glm::mat4>& out) const;

    // Pose times inverse bind: what the vertex shader multiplies by.
    bool skinningMatrices(std::uint32_t clip, float time, std::vector<glm::mat4>& out) const;

    // One sampling that yields both: the model-space pose and the skinning
    // matrices. `rootOut` receives the root joint's translation.
    bool sample(std::uint32_t clip, float time, std::vector<glm::mat4>& skin, glm::vec3& rootOut) const;

    // Root joint translation at `time`, model space. The difference between
    // two samples is the root motion the controller should apply.
    bool rootTranslation(std::uint32_t clip, float time, glm::vec3& out) const;

    static constexpr std::uint32_t kNoClip = 0xffffffffu;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A clip with the commitment windows doc 18 is built on. Times are seconds
// from the clip start; the montage is done after `recoveryEnd`.
struct Montage {
    std::uint32_t clip = AnimationSet::kNoClip;
    float activeStart = 0.0f; // wind-up ends
    float activeEnd = 0.0f;   // recovery begins
    float recoveryEnd = 0.0f; // montage over
};

enum class MontagePhase : std::uint8_t { Idle, Windup, Active, Recovery };

// Advances on the fixed step and reports phase changes on the exact tick they
// happen, which is what makes frame data testable.
struct MontageState {
    Montage montage;
    float time = 0.0f;
    MontagePhase phase = MontagePhase::Idle;

    void start(const Montage& m);
    // Returns the phase entered this tick, or Idle when nothing changed.
    MontagePhase advance(float dt);
    bool active() const { return phase == MontagePhase::Active; }
    bool busy() const { return phase != MontagePhase::Idle; }
};

}
