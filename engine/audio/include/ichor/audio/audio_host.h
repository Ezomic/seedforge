#pragma once

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

    virtual void play(std::uint32_t sound, glm::dvec3 position, bool spatial) = 0;
    virtual void setListener(glm::dvec3 position, glm::quat rotation) = 0;
};

}
