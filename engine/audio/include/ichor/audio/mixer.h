#pragma once

#include "ichor/assets/bundle.h"
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
    static constexpr std::uint32_t kRate = 48000;
    static constexpr std::uint32_t kChannels = 2;
    static constexpr std::uint32_t kNoSound = 0xffffffffu;

    // Appends; the bundle's sounds get indices continuing from clipCount().
    void addBundle(const SceneBundle& bundle);
    std::uint32_t clipCount() const;
    std::uint32_t findClip(std::string_view name) const;

    static constexpr std::size_t kMaxVoices = 64;

    // Positions stay double until the listener-relative offset is formed, so
    // far from the origin the panning does not jitter.
    void play(std::uint32_t clip, glm::dvec3 position, float gain, bool spatial);
    void setListener(glm::dvec3 position, glm::vec3 forward, glm::vec3 right);
    void setReverb(const ReverbSettings& settings);

    // Renders `frames` stereo frames, advancing every voice. Safe to call from
    // a device thread; play() and setListener() take the same lock.
    void mix(float* interleaved, std::uint32_t frames);

    std::size_t activeVoices() const;

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
    };

    mutable std::mutex mutex_;
    std::vector<Clip> clips_;
    std::vector<Voice> voices_;
    Reverb reverb_;
    glm::dvec3 listenerPosition_{0.0};
    glm::vec3 listenerForward_{0.0f, 0.0f, -1.0f};
    glm::vec3 listenerRight_{1.0f, 0.0f, 0.0f};
};

}
