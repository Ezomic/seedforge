#include "lighting.hlsli"

struct PSIn {
    float4 clip : SV_Position;
    float3 direction : TEXCOORD0;
};

struct PSOut {
    float4 color : SV_Target;
    float depth : SV_Depth;
};

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

// The water surface, found by intersecting each view ray with the plane at the water height.
// The depth is written from that point, so land in front of the water hides it.
PSOut main(PSIn i)
{
    const float level = frame.waterParams.x;
    const float height = level - frame.cameraWorld.y;
    const float3 direction = normalize(i.direction);
    // Only looking down at the water from above it.
    if (frame.waterColor.w <= 0.0 || height >= 0.0 || direction.y >= -1e-4) {
        discard;
    }
    const float travelled = height / direction.y;
    const float3 position = direction * travelled;
    const float4 clip = mul(frame.viewProj, float4(position, 1.0));
    // A little nearer than exact, so the waterline does not speckle where it meets the land.
    const float depth = clip.z / clip.w - 2e-6;
    if (depth >= 1.0) {
        discard;
    }

    const float time = frame.skyParams.z;
    const float2 slope = ripple(position.xz + frame.cameraWorld.xz, time, travelled);
    const float3 normal = normalize(float3(-slope.x, 1.0, -slope.y));
    const float3 view = -direction;
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

    PSOut o;
    o.color = toneMap(applyFog(lerp(body, reflection, fresnel), position, i.clip.xy));
    o.color.a = lerp(0.7, 1.0, fresnel);
    o.depth = depth;
    return o;
}
