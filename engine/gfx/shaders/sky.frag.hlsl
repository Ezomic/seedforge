#include "common.hlsli"

struct PSIn {
    float4 clip : SV_Position;
    float3 direction : TEXCOORD0;
};

float hash13(float3 p)
{
    return frac(sin(dot(p, float3(12.9898, 78.233, 37.719))) * 43758.5453);
}

// A field of stars: the sky direction is scaled into a lattice, each cell may hold one star at a
// jittered spot, brighter for rarer hashes, and the light of each flickers a little with time.
float3 stars(float3 dir, float time)
{
    const float3 p = dir * 90.0;
    const float3 cell = floor(p);
    const float h = hash13(cell);
    if (h < 0.955) {
        return 0.0;
    }
    const float3 jitter = float3(hash13(cell + 1.7), hash13(cell + 5.3), hash13(cell + 9.1)) - 0.5;
    const float d = length(frac(p) - 0.5 - jitter * 0.5);
    const float size = smoothstep(0.16, 0.0, d);
    const float bright = (h - 0.955) / 0.045;
    const float twinkle = 0.8 + 0.2 * sin(time * 2.5 + h * 400.0);
    // A slight tint, from blue-white to warm.
    const float3 tint = lerp(float3(0.75, 0.85, 1.0), float3(1.0, 0.9, 0.75), hash13(cell + 3.3));
    return tint * size * (0.25 + bright) * twinkle;
}

float4 main(PSIn i) : SV_Target
{
    const float3 dir = normalize(i.direction);
    const float up = dir.y;

    // Zenith overhead, horizon at eye level, and below the horizon the ground
    // haze darkens towards a floor so the far edge of the world has no seam.
    float3 color = lerp(frame.skyHorizon.rgb, frame.skyZenith.rgb, pow(saturate(up), 0.45));
    color = lerp(color, frame.skyHorizon.rgb * 0.45, saturate(-up * 3.0));

    // Stars come out as the zenith darkens, and fade into the haze towards the horizon.
    const float zenithLuma = dot(frame.skyZenith.rgb, float3(0.2126, 0.7152, 0.0722));
    const float night = 1.0 - smoothstep(0.02, 0.12, zenithLuma);
    if (night > 0.0 && up > 0.0) {
        color += stars(dir, frame.skyParams.z) * night * smoothstep(0.0, 0.2, up) * 2.5;
    }

    // The sun by day, the moon by night: a disc and a soft glow around it.
    const float along = dot(dir, normalize(frame.sunDirection.xyz));
    const float disc = smoothstep(0.99955, 0.99975, along);
    // The moon is a much fainter light than the sun, and its haze is a lot smaller.
    const float moonlit = night;
    const float glow = pow(saturate(along), lerp(48.0, 220.0, moonlit)) * lerp(0.35, 0.18, moonlit);
    // Scaled by the light's own intensity, so the disc fades out with the light when the
    // sun hands over to the moon and never pops at the switch.
    const float3 light = frame.sunColor.rgb * (frame.sunColor.w * 6.0);
    color += light * (disc + glow) * smoothstep(-0.05, 0.02, up);

    if (isUnderwater()) {
        color = underwaterColor();
    }
    return toneMap(color);
}
