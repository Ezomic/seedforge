#ifndef ICHOR_WATER_HLSLI
#define ICHOR_WATER_HLSLI

#include "lighting.hlsli"

// Slope of the surface at a world position: a few crossing swells, flatter with distance
// so far water does not shimmer.
float2 ripple(float2 p, float time, float distance)
{
    float2 slope = 0.0;
    slope += float2(0.31, 0.12) * cos(dot(p, float2(0.31, 0.12)) * 1.0 + time * 1.3);
    slope += float2(-0.17, 0.26) * cos(dot(p, float2(-0.17, 0.26)) * 1.7 + time * 1.7);
    slope += float2(0.57, -0.41) * cos(dot(p, float2(0.57, -0.41)) * 2.3 - time * 2.1) * 0.5;
    slope += float2(-0.83, -0.35) * cos(dot(p, float2(-0.83, -0.35)) * 3.1 + time * 2.7) * 0.3;
    return slope * 0.05 * rcp(1.0 + distance * 0.02);
}

// The colour and opacity of a point on a water surface. `position` is camera-relative, which is
// also the view ray from the camera to the point.
float4 shadeWater(float3 position, float2 pixel)
{
    const float travelled = length(position);
    const float3 direction = position / max(travelled, 1e-4);
    const float2 slope = ripple(position.xz + frame.cameraWorld.xz, frame.skyParams.z, travelled);
    float3 normal = normalize(float3(-slope.x, 1.0, -slope.y));
    const float3 view = -direction;
    // A surface seen from underneath, such as a river on a hillside above the viewer, is shaded
    // from its other side, so it reads as water and not as a bright mirror.
    if (dot(normal, view) < 0.0) {
        normal = -normal;
    }
    const float cosView = saturate(dot(normal, view));
    const float fresnel = 0.02 + 0.98 * pow(1.0 - cosView, 5.0);

    // What the surface mirrors: the sky towards the reflected ray, and the sun glint.
    const float3 mirrored = reflect(direction, normal);
    const float up = saturate(mirrored.y);
    const float3 sky = lerp(frame.skyHorizon.rgb, frame.skyZenith.rgb, pow(up, 0.45));
    const float glint = pow(saturate(dot(mirrored, normalize(frame.sunDirection.xyz))), 400.0);
    const float3 sunlight = frame.sunColor.rgb * frame.sunColor.w;
    const float3 reflection = sky + sunlight * glint * 40.0;

    // What lies below: the deep colour, lit by the sky and, a little, the sun.
    const float3 body = frame.waterColor.rgb * (frame.ambient.rgb * 2.0 + sunlight * 0.06 * saturate(frame.sunDirection.y));

    float4 color = toneMap(applyFog(lerp(body, reflection, fresnel), position, pixel));
    color.a = lerp(0.7, 1.0, fresnel);
    return color;
}

#endif
