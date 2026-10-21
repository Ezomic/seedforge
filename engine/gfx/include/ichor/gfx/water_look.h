#pragma once

#include <glm/glm.hpp>

namespace ichor {

// The colour of the forged rivers' and lakes' water body at a depth, before lighting: turquoise in the
// shallows, deep blue by kWaterTintDepth metres. water.hlsli holds the same numbers (depthTint), and
// test_water_look keeps the two in step.
inline constexpr glm::vec3 kWaterShallowTint{0.10f, 0.52f, 0.50f};
inline constexpr glm::vec3 kWaterDeepTint{0.015f, 0.10f, 0.26f};
inline constexpr float kWaterTintDepth = 4.0f;

inline glm::vec3 waterDepthTint(float depth)
{
    const float t = glm::clamp(depth / kWaterTintDepth, 0.0f, 1.0f);
    return glm::mix(kWaterShallowTint, kWaterDeepTint, t * t * (3.0f - 2.0f * t));
}

}
