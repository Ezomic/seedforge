#include "lighting.hlsli"

[[vk::binding(0, 1)]] Texture2D baseColorTex;
[[vk::binding(1, 1)]] Texture2D metalRoughTex;
[[vk::binding(2, 1)]] Texture2D normalTex;
[[vk::binding(3, 1)]] SamplerState materialSampler;

struct PSIn {
    float4 clip : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float rise : TEXCOORD4;
};

float4 main(PSIn i) : SV_Target
{
    const float4 baseSample = baseColorTex.Sample(materialSampler, i.uv);
    float3 baseColor = baseSample.rgb * push.baseColorFactor.rgb;
    // Objects that stand on the ground take its colour at their foot, and darken where they meet
    // it, so they sit in the terrain instead of on it. skin.z marks the ones that do.
    if (frame.groundParams.w > 0.0 && push.skin.z != 0) {
        const float foot = 1.0 - smoothstep(0.0, frame.groundParams.w, i.rise);
        baseColor = lerp(baseColor, frame.groundParams.rgb, foot * 0.85);
        baseColor *= 1.0 - 0.45 * foot * foot;
    }
    const float2 metalRough = metalRoughTex.Sample(materialSampler, i.uv).bg;
    const float metallic = saturate(metalRough.x * push.params.x);
    const float roughness = clamp(metalRough.y * push.params.y, 0.04, 1.0);

    float3 n = normalize(i.normal);
    if (push.params.z > 0.5) {
        const float3 t = normalize(i.tangent.xyz - n * dot(n, i.tangent.xyz));
        const float3 b = cross(n, t) * i.tangent.w;
        const float3 tn = normalTex.Sample(materialSampler, i.uv).xyz * 2.0 - 1.0;
        n = normalize(mul(tn, float3x3(t, b, n)));
    }
    return toneMap(applyFog(shade(i.worldPos, n, normalize(i.normal), baseColor, metallic, roughness), i.worldPos, i.clip.xy));
}
