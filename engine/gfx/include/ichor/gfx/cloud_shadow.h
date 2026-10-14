#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

// Cloud shadows read the same drifting cloud field the sky draws. The shader works in the
// sky's plane units, with the same function and the same drift; these constants tie the ground to that
// plane and are mirrored in cloud.hlsli.
inline constexpr double kCloudHeight = 1200.0;                    // metres over the sea, where the layer sits
inline constexpr double kCloudPlanePerMetre = 0.005;              // plane units per ground metre: a cell is 200 m, so patches fit in a view
inline constexpr float kCloudDrift = 0.03f;                       // plane units per second, along the wind
inline constexpr float kCloudTimeWrap = 3600.0f;                  // seconds; the drift phase restarts here
inline constexpr float kCloudShadowFloor = 0.35f;                 // light that always gets through

struct CloudShadow {
    float strength = 0.0f;  // 0 casts nothing; the shader darkens by strength times the cloud density
    glm::vec2 offset{0.0f}; // plane position of the camera, plus the wind's drift
};

inline float cloudSmoothstep(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline CloudShadow cloudShadowFor(glm::vec3 sunDirection, float cover, glm::dvec3 camera, glm::vec2 windDirection, float time)
{
    CloudShadow shadow;
    const float length = glm::length(sunDirection);
    if (!(length > 1e-6f) || !std::isfinite(cover)) {
        return shadow;
    }
    // The shader slides each point along the sun to the cloud, so a sun near the horizon would
    // reach for ever; the shadow fades out before that and is gone with the sun.
    const float elevation = sunDirection.y / length;
    const float sun = cloudSmoothstep(0.05f, 0.3f, elevation);
    // Heavier cloud throws darker shadows, up to broken cloud; under a solid overcast the sky is flat and so is the pattern.
    const float broken = cloudSmoothstep(0.1f, 0.8f, cover);
    const float overcast = 1.0f - 0.45f * cloudSmoothstep(0.8f, 1.0f, cover);
    shadow.strength = (1.0f - kCloudShadowFloor) * sun * broken * overcast;

    const float phase = std::isfinite(time) ? std::fmod(time, kCloudTimeWrap) : 0.0f;
    const double x = std::isfinite(camera.x) ? camera.x : 0.0;
    const double z = std::isfinite(camera.z) ? camera.z : 0.0;
    shadow.offset = glm::vec2(static_cast<float>(x * kCloudPlanePerMetre), static_cast<float>(z * kCloudPlanePerMetre)) + windDirection * (phase * kCloudDrift);
    return shadow;
}

}
