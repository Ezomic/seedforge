#pragma once

#include "ichor/audio/ducking.h"
#include "ichor/audio/reverb.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string_view>

namespace ichor {

struct SceneBundle;

// What the loop talks to. The null host mixes into a buffer on request; the
// miniaudio host mixes on the device thread.
class AudioHost {
public:
    virtual ~AudioHost() = default;

    virtual bool init() = 0;
    virtual void addBundle(const SceneBundle& bundle) = 0;
    virtual std::uint32_t findSound(std::string_view name) const = 0;

    // The gain scales the cue, 1 being the clip as cooked.
    // A categorised cue is capped by its category's ceiling and ducks the ambience while it plays.
    virtual void play(std::uint32_t sound, glm::dvec3 position, bool spatial, float gain = 1.0f,
        SoundCategory category = SoundCategory::None) = 0;
    // A looping, non-spatial bed under everything else. Calling it again only retargets the gain,
    // which eases in, so it can be called every frame.
    virtual void setAmbience(std::uint32_t sound, float gain) = 0;
    // Fades the music voices that are playing to silence over `seconds`. Hosts without a mixer ignore it.
    virtual void fadeOutMusic(float seconds) { (void)seconds; }
    // Scales everything heard, easing toward the gain so it never clicks; 1 is the mix as it is, 0 silence.
    // NaN counts as silence and the rest is clamped to 0..1. Hosts without a mixer ignore it.
    virtual void setMasterGain(float gain) { (void)gain; }
    virtual void setListener(glm::dvec3 position, glm::quat rotation) = 0;
    virtual void setReverb(const ReverbSettings& settings) = 0;
};

}
