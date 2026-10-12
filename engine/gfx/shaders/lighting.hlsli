#ifndef ICHOR_LIGHTING_HLSLI
#define ICHOR_LIGHTING_HLSLI

#include "common.hlsli"

[[vk::binding(1, 0)]] Texture2DArray<float> shadowMap;
[[vk::binding(2, 0)]] SamplerComparisonState shadowSampler;

static const float PI = 3.14159265359;

float distributionGgx(float nDotH, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float geometrySmith(float nDotV, float nDotL, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    const float gv = nDotV / (nDotV * (1.0 - k) + k);
    const float gl = nDotL / (nDotL * (1.0 - k) + k);
    return gv * gl;
}

float3 fresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float sampleShadow(float3 worldPos, float3 normal, float nDotL)
{
    const float viewDepth = -mul(frame.view, float4(worldPos, 1.0)).z;
    int cascade = 0;
    if (viewDepth > frame.cascadeSplits.x) cascade = 1;
    if (viewDepth > frame.cascadeSplits.y) cascade = 2;
    if (viewDepth > frame.cascadeSplits.z) return 1.0;

    const float offsetScale = frame.shadowParams.z * (cascade + 1) * (1.0 - nDotL * 0.6);
    const float4 light = mul(frame.cascadeViewProj[cascade], float4(worldPos + normal * offsetScale, 1.0));
    const float2 uv = float2(light.x * 0.5 + 0.5, 0.5 - light.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0)) return 1.0;
    const float depth = light.z - frame.shadowParams.y;

    const float texel = frame.shadowParams.x;
    float lit = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            lit += shadowMap.SampleCmpLevelZero(shadowSampler, float3(uv + float2(x, y) * texel, cascade), depth);
        }
    }
    return lit / 9.0;
}

// Cook-Torrance with a hemisphere ambient; positions are camera-relative so
// the view vector is just the negated position.
float3 shade(float3 worldPos, float3 n, float3 geometricNormal, float3 baseColor, float metallic, float roughness)
{
    const float3 v = normalize(-worldPos);
    const float3 l = normalize(frame.sunDirection.xyz);
    const float3 h = normalize(v + l);
    const float nDotL = saturate(dot(n, l));
    const float nDotV = max(dot(n, v), 1e-4);
    const float nDotH = saturate(dot(n, h));
    const float hDotV = saturate(dot(h, v));

    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    const float3 f = fresnelSchlick(hDotV, f0);
    const float d = distributionGgx(nDotH, roughness);
    const float g = geometrySmith(nDotV, nDotL, roughness);
    const float3 specular = (d * g * f) / max(4.0 * nDotV * nDotL, 1e-4);
    const float3 kd = (1.0 - f) * (1.0 - metallic);
    const float3 diffuse = kd * baseColor / PI;

    const float shadow = nDotL > 0.0 ? sampleShadow(worldPos, geometricNormal, nDotL) : 0.0;
    const float3 radiance = frame.sunColor.rgb * frame.sunColor.w;
    float3 color = (diffuse + specular) * radiance * nDotL * shadow;

    const float sky = n.y * 0.5 + 0.5;
    color += frame.ambient.rgb * lerp(0.35, 1.0, sky) * baseColor * (1.0 - metallic * 0.6);
    return color;
}

// Exponential fog by distance from the camera; positions are camera-relative.
float3 applyFog(float3 color, float3 worldPos)
{
    // With a height falloff the density at a point is base * exp(-k * (y - reference)); the
    // optical depth along the ray from the camera is the integral of that, which has a
    // closed form. The reference height is where the fog has its stated density.
    const float k = frame.wind.w;
    float depth = frame.fogColor.w * length(worldPos);
    if (k > 0.0) {
        const float cameraY = frame.cameraWorld.y;
        const float t = k * worldPos.y;
        const float integral = abs(t) > 1e-3 ? (1.0 - exp(-t)) / t : 1.0;
        depth *= exp(-k * (cameraY - 120.0)) * integral;
    }
    return lerp(color, frame.fogColor.rgb, 1.0 - exp(-depth));
}

#endif
