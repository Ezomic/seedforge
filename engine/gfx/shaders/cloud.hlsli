#ifndef ICHOR_CLOUD_HLSLI
#define ICHOR_CLOUD_HLSLI

#include "common.hlsli"

// The cloud field, shared by the sky overhead and the shadows it throws on the ground, so one
// matches the other. Plane units are the sky's: see gfx/cloud_shadow.h for the ground mapping.

float hash12(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(float2 p)
{
    const float2 cell = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash12(cell), hash12(cell + float2(1, 0)), u.x), lerp(hash12(cell + float2(0, 1)), hash12(cell + float2(1, 1)), u.x), u.y);
}

float cloudNoise(float2 p)
{
    float sum = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 5; ++octave) {
        sum += valueNoise(p) * amplitude;
        p = p * 2.03 + 17.0;
        amplitude *= 0.5;
    }
    return sum;
}

// How much of the layer is cloud at a point of the plane: 0 clear, 1 solid.
float cloudCoverage(float2 plane, float cover)
{
    return smoothstep(1.0 - cover, 1.0 - cover + 0.18, cloudNoise(plane));
}

static const float CLOUD_HEIGHT = 1200.0;
static const float CLOUD_PLANE_PER_METRE = 0.005;

// How much of the sun's light reaches a point, after the clouds in the way. `worldPos` is
// camera-relative; the frame carries the shadow's strength and where the camera sits in the plane.
float cloudShadowFactor(float3 worldPos)
{
    const float strength = frame.cloudShadow.x;
    if (strength <= 0.0) {
        return 1.0;
    }
    // Slide along the sun to the layer, the way a ray of light would come down.
    const float3 sun = normalize(frame.sunDirection.xyz);
    const float rise = max(CLOUD_HEIGHT - (worldPos.y + frame.cameraWorld.y), 0.0);
    const float2 ground = worldPos.xz + sun.xz / max(sun.y, 0.1) * rise;
    const float2 plane = ground * CLOUD_PLANE_PER_METRE + frame.cloudShadow.yz;
    return 1.0 - strength * cloudCoverage(plane, frame.shaftParams.z);
}

#endif
