#include "lighting.hlsli"

// The three splat layers ride in the material set's three slots.
[[vk::binding(0, 1)]] Texture2D grassTex;
[[vk::binding(1, 1)]] Texture2D rockTex;
[[vk::binding(2, 1)]] Texture2D snowTex;
[[vk::binding(3, 1)]] SamplerState materialSampler;

struct PSIn {
    float4 clip : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
};

float4 main(PSIn i) : SV_Target
{
    const float3 n = normalize(i.normal);
    // Positions are camera-relative; the frame carries where the camera is.
    const float height = i.worldPos.y + frame.cameraWorld.y;
    const float slope = 1.0 - n.y;
    const float rock = smoothstep(0.28, 0.5, slope);
    const float snow = smoothstep(230.0, 300.0, height) * (1.0 - rock * 0.6);
    const float grass = saturate(1.0 - rock - snow);

    // A second, larger tiling breaks the repetition of the near one.
    const float3 g = lerp(grassTex.Sample(materialSampler, i.uv).rgb, grassTex.Sample(materialSampler, i.uv * 0.13).rgb, 0.4);
    const float3 r = lerp(rockTex.Sample(materialSampler, i.uv * 0.7).rgb, rockTex.Sample(materialSampler, i.uv * 0.09).rgb, 0.4);
    const float3 s = snowTex.Sample(materialSampler, i.uv * 0.5).rgb;
    const float3 baseColor = g * grass + r * rock + s * snow;
    const float roughness = lerp(0.95, 0.7, snow);
    return toneMap(shade(i.worldPos, n, n, baseColor, 0.0, roughness));
}
