#ifndef ICHOR_WETNESS_HLSLI
#define ICHOR_WETNESS_HLSLI

#include "common.hlsli"

// Rain soaks the ground, bark and stone: they darken and gain a little gloss. Normals and
// shape stay as they were, and with no wetness nothing changes.
void applyWetness(inout float3 baseColor, inout float roughness, float metallic)
{
    const float wet = frame.shadowParams.w * (1.0 - metallic);
    baseColor *= lerp(1.0, 0.65, wet);
    roughness *= lerp(1.0, 0.6, wet);
}

#endif
