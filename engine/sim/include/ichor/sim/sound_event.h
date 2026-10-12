#pragma once

#include "ichor/sim/components.h"

#include <glm/vec3.hpp>

#include <cstdint>

namespace ichor {

constexpr std::uint32_t kNoSound = 0xffffffffu;

// Raised by the simulation on the tick something audible happens. The loop
// hands them to the audio host; the scene never touches a device.
struct SoundEvent {
    StableId emitter;
    std::uint32_t sound = kNoSound;
    glm::dvec3 position{0.0};
};

}
