#pragma once

#include "ichor/assets/bundle_types.h"
#include "ichor/audio/ducking.h"
#include "ichor/audio/reverb.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <mutex>
#include <vector>

namespace ichor {

// A software mixer: mono clips, stereo float output at 48 kHz, one-shot
// voices, distance attenuation and panning against a listener. It is pure
// arithmetic, so the tests render it to a buffer and check the numbers.
class Mixer {
public:
    static constexpr std::uint32_t kRate = Reverb::kRate;
    static constexpr std::uint32_t kChannels = 2;
    static constexpr std::uint32_t kNoSound = 0xffffffffu;

    // Appends; the bundle's sounds get indices continuing from clipCount().
    void addBundle(const SceneBundle& bundle);
    std::uint32_t clipCount() const;
    std::uint32_t findClip(std::string_view name) const;

    static constexpr std::size_t kMaxVoices = 64;

    // Positions stay double until the listener-relative offset is formed, so
    // far from the origin the panning does not jitter.
    // A categorised voice is capped by its ceiling and ducks the looping beds while it plays (see ducking.h). A NaN gain plays silent.
    void play(std::uint32_t clip, glm::dvec3 position, float gain, bool spatial,
        SoundCategory category = SoundCategory::None);
    // One looping, non-spatial voice per clip, made on first use. The gain eases toward the target
    // (a time constant of about a quarter second) so a changing value never clicks; NaN counts as silence and the rest is clamped to 0..1.
    void setLoopGain(std::uint32_t clip, float gain);
    // Fades every playing voice of the category to silence over `seconds` and ends it; a new voice of the category is not affected.
    void fadeOutCategory(SoundCategory category, float seconds);
    // Scales the whole mix after the reverb, ramping linearly over a tenth of a second so it never clicks and lands
    // exactly; NaN counts as silence and the rest is clamped to 0..1.
    void setMasterGain(float gain);
    void setListener(glm::dvec3 position, glm::vec3 forward, glm::vec3 right);
    void setReverb(const ReverbSettings& settings);

    // Renders `frames` stereo frames, advancing every voice. Safe to call from
    // a device thread; play() and setListener() take the same lock.
    void mix(float* interleaved, std::uint32_t frames);

    std::size_t activeVoices() const;
    std::size_t activeLoops() const;

private:
    struct Clip {
        std::string name;
        std::uint32_t sampleRate = 0;
        std::vector<std::int16_t> samples;
    };
    struct Voice {
        std::uint32_t clip = 0;
        double cursor = 0.0;
        glm::dvec3 position{0.0};
        float gain = 1.0f;
        bool spatial = false;
        SoundCategory category = SoundCategory::None;
        float fade = 1.0f;     // multiplies the voice; only falls, and only after fadeOutCategory
        float fadeStep = 0.0f; // taken off fade each frame
    };

    struct Loop {
        std::uint32_t clip = 0;
        double cursor = 0.0;
        float gain = 0.0f;
        float target = 0.0f;
    };

    mutable std::mutex mutex_;
    std::vector<Clip> clips_;
    std::vector<Voice> voices_;
    std::vector<Loop> loops_;
    Reverb reverb_;
    float duck_ = 1.0f;
    float master_ = 1.0f;
    float masterTarget_ = 1.0f;
    glm::dvec3 listenerPosition_{0.0};
    glm::vec3 listenerForward_{0.0f, 0.0f, -1.0f};
    glm::vec3 listenerRight_{1.0f, 0.0f, 0.0f};
};

}
