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

// Slope of a river's surface: ridges that run across the current and travel down it. `flow` is the
// current on the ground plane, its length how much of a river there is (zero on lakes and sea).
// Pure in the position, the flow and the time. The time is wrapped in cycles, and the caller keeps
// it under an hour, which holds each layer to whole cycles so it does not jump when that wraps.
// The camera's world position enters as a wrapped phase, but the phase is taken along the flow
// direction, which turns with the interpolated flow on bends; far from the origin that bends the
// ridges in proportion to the distance, so very far from the origin they lose their shape. Each layer fades out
// with distance, where it would only shimmer.
float2 riverRipple(float2 relative, float2 cameraWorld, float2 flow, float time, float distance)
{
    const float strength = saturate(length(flow));
    // The finer ridges give out sooner than the coarse ones, which are wider than a pixel for longer.
    const float coarseFade = 1.0 - smoothstep(6.0, 40.0, distance);
    const float fineFade = 1.0 - smoothstep(2.0, 14.0, distance);
    if (strength * coarseFade <= 0.0) {
        return 0.0;
    }
    const float2 along = flow / max(length(flow), 1e-4);
    const float2 across = float2(-along.y, along.x);
    // A swell cycle every 1.6 m moving 0.8 m/s, and a finer one every 0.9 m moving a little slower.
    const float coarse = dot(relative, along) / 1.6 + frac(dot(cameraWorld, along) / 1.6) - frac(time * 0.5);
    const float fine = dot(relative, along) / 0.9 + frac(dot(cameraWorld, along) / 0.9) - frac(time * 0.75);
    // The ridges are broken up across the current, so they read as water and not as corduroy.
    const float breakup = 0.65 + 0.35 * sin(6.2831853 * (dot(relative, across) / 3.1 + frac(dot(cameraWorld, across) / 3.1)));
    const float wave = cos(6.2831853 * coarse) * 0.7 * coarseFade + cos(6.2831853 * fine) * 0.4 * fineFade;
    return along * wave * breakup * strength * 0.3;
}

// The forged rivers and lakes: the colour of the water body at a depth, turquoise in the shallows to deep
// blue by kTintDepth metres. The same numbers are in ichor/gfx/water_look.h, which the tests hold to.
static const float3 kShallowTint = float3(0.10, 0.52, 0.50);
static const float3 kDeepTint = float3(0.015, 0.10, 0.26);
static const float kTintDepth = 4.0;

float3 depthTint(float depth)
{
    return lerp(kShallowTint, kDeepTint, smoothstep(0.0, kTintDepth, depth));
}

// The colour and opacity of a point on a water surface. `position` is camera-relative, which is
// also the view ray from the camera to the point; `depth` is how far the surface stands over the
// ground there, which softens the shore: clear and pale in the shallows, a thin band of foam at the edge.
float4 shadeWater(float3 position, float2 pixel, float depth, float2 flow = 0.0, bool forgedLook = false, float bank = 1.0)
{
    const float travelled = length(position);
    const float3 direction = position / max(travelled, 1e-4);
    float2 slope = ripple(position.xz + frame.cameraWorld.xz, frame.skyParams.z, travelled);
    slope += riverRipple(position.xz, frame.cameraWorld.xz, flow, frame.skyParams.z, travelled);
    if (forgedLook) {
        slope *= 3.0;
    }
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
    const float3 light = frame.ambient.rgb * 2.0 + sunlight * 0.06 * saturate(frame.sunDirection.y);
    const float3 deep = frame.waterColor.rgb * light;
    const float3 pale = frame.waterColor.rgb * 3.0 * light + float3(0.02, 0.035, 0.03) * light;
    float3 body = lerp(pale, deep, smoothstep(0.0, 2.0, depth));
    if (forgedLook) {
        const float3 lit = frame.ambient.rgb * 4.0 + sunlight * 0.5 * saturate(frame.sunDirection.y);
        body = depthTint(depth) * lit;
    }

    float3 surface = lerp(body, reflection, fresnel);
    // A band of foam where the water meets the bank.
    const float foam = smoothstep(0.0, 0.04, depth) * (1.0 - smoothstep(0.08, 0.22, depth));
    const float3 foamColour = float3(0.9, 0.95, 1.0) * (frame.ambient.rgb * 3.0 + sunlight * 0.12);
    if (forgedLook) {
        // A faint, broken wet line at the bank and, on a river, pale streaks that run with the current.
        const float2 world = position.xz + frame.cameraWorld.xz;
        const float broken = 0.5 + 0.5 * sin(world.x * 1.9 + frame.skyParams.z * 0.6) * sin(world.y * 2.3 - frame.skyParams.z * 0.5);
        const float edge = smoothstep(0.0, 0.06, depth) * (1.0 - smoothstep(0.06, 0.4, depth));
        surface = lerp(surface, foamColour, edge * broken * 0.3);
        const float current = saturate(length(flow));
        if (current > 0.0) {
            const float2 along = flow / max(length(flow), 1e-4);
            const float2 across = float2(-along.y, along.x);
            const float bend = sin(dot(world, along) * 0.45 - frame.skyParams.z * 1.2) * 1.4;
            const float streak = smoothstep(0.55, 1.0, sin(dot(world, across) * 2.1 + bend));
            surface += foamColour * streak * current * 0.08 * (1.0 - smoothstep(10.0, 60.0, travelled));
        }
    } else {
        surface = lerp(surface, foamColour, foam * 0.55);
    }

    float4 color = toneMap(applyFog(surface, position, pixel));
    if (forgedLook) {
        // Clear to the bed at the waterline, thickening over the first half metre, then nearly opaque in the deeps.
        // The shore also ramps up from the edge samples, so it is soft where the sample grid steps.
        color.a = smoothstep(0.0, 0.5, depth) * bank * lerp(0.55, 0.97, smoothstep(0.0, 1.5, depth));
        color.a = lerp(color.a, 1.0, fresnel * 0.5 * smoothstep(0.0, 0.5, depth));
        return color;
    }
    // Open water is nearly opaque at a glance; the shallows and the very edge clear to the bed.
    color.a = lerp(0.7, 1.0, fresnel) * lerp(0.65, 1.0, smoothstep(0.0, 0.8, depth)) * smoothstep(0.0, 0.05, depth + 0.02);
    return color;
}

#endif
