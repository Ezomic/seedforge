#include "common.hlsli"

struct PSIn {
    float4 clip : SV_Position;
    float3 direction : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target
{
    const float3 dir = normalize(i.direction);
    const float up = dir.y;

    // Zenith overhead, horizon at eye level, and below the horizon the ground
    // haze darkens towards a floor so the far edge of the world has no seam.
    float3 color = lerp(frame.skyHorizon.rgb, frame.skyZenith.rgb, pow(saturate(up), 0.45));
    color = lerp(color, frame.skyHorizon.rgb * 0.45, saturate(-up * 3.0));

    // The sun by day, the moon by night: a disc and a soft glow around it.
    const float along = dot(dir, normalize(frame.sunDirection.xyz));
    const float disc = smoothstep(0.99955, 0.99975, along);
    const float glow = pow(saturate(along), 48.0) * 0.35;
    // Scaled by the light's own intensity, so the disc fades out with the light when the
    // sun hands over to the moon and never pops at the switch.
    const float3 light = frame.sunColor.rgb * (frame.sunColor.w * 6.0);
    color += light * (disc + glow) * smoothstep(-0.05, 0.02, up);

    return toneMap(color);
}
