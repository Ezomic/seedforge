#include "lighting.hlsli"
#include "wetness.hlsli"

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

float luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// The rock layer projected along each axis and mixed by how much the surface faces it, so a cliff
// shows the rock's own grain and not the top-down texture smeared down its face.
float3 triplanarRock(float3 position, float3 weights, float scale)
{
    return rockTex.Sample(materialSampler, position.zy * scale).rgb * weights.x
        + rockTex.Sample(materialSampler, position.xz * scale).rgb * weights.y
        + rockTex.Sample(materialSampler, position.xy * scale).rgb * weights.z;
}

float4 main(PSIn i) : SV_Target
{
    const float3 n = normalize(i.normal);
    // Positions are camera-relative; the frame carries where the camera is.
    const float height = i.worldPos.y + frame.cameraWorld.y;
    const float slope = 1.0 - n.y;
    float rock = smoothstep(0.28, 0.5, slope);
    float snow = smoothstep(230.0, 300.0, height) * (1.0 - rock * 0.6);
    float grass = saturate(1.0 - rock - snow);
    const bool forged = i.tangent.w < 0.0;
    float3 baseColor;
    float bankAmount = 0.0;
    if (forged) {
        // A forged world: the cell's biome gave each vertex its grass, rock and snow weights, which sum to 1,
        // and the ground its sand and wet bank in the uv. The vertex stage normalised the weights as a direction,
        // so dividing by their sum gives them back.
        const float3 biome = abs(i.tangent.xyz) / max(abs(i.tangent.x) + abs(i.tangent.y) + abs(i.tangent.z), 1e-4);
        // Textures tile by world position, so they do not depend on how the cells were cut.
        const float3 place = float3(i.worldPos.x + frame.cameraWorld.x, height, i.worldPos.z + frame.cameraWorld.z);
        const float2 tile = place.xz / 8.0;
        float3 faces = pow(abs(n), 4.0);
        faces /= max(faces.x + faces.y + faces.z, 1e-4);
        const float3 g = lerp(grassTex.Sample(materialSampler, tile).rgb, grassTex.Sample(materialSampler, tile * 0.13).rgb, 0.4);
        const float3 r = lerp(triplanarRock(place, faces, 0.0875), triplanarRock(place, faces, 0.01125), 0.4);
        const float3 s = snowTex.Sample(materialSampler, tile * 0.5).rgb;
        // The rock grain moves the line where a slope turns to cliff, so the edge of the rock is ragged.
        const float steep = smoothstep(0.24, 0.5, slope + (luma(r) - 0.5) * 0.12);
        rock = saturate(biome.y + steep);
        snow = biome.z * (1.0 - steep * 0.6);
        grass = biome.x * (1.0 - steep);
        const float total = max(grass + rock + snow, 1e-4);
        float3 mixture = float3(grass, rock, snow) / total;
        // Where two layers meet, the one whose texture is brighter there wins a little more, so a border
        // interlocks like patches of the two and does not fade like a smear.
        const float3 grain = float3(luma(g), luma(r), luma(s)) * 0.35;
        const float3 score = mixture + grain;
        const float top = max(score.x, max(score.y, score.z)) - 0.2;
        mixture = max(score - top, 0.0);
        mixture /= max(mixture.x + mixture.y + mixture.z, 1e-4);
        grass = mixture.x;
        rock = mixture.y;
        snow = mixture.z;
        baseColor = g * grass + r * rock + s * snow;
        // Sand lies on the low flat ground by a lake or the sea, and the bank of a river is darker, wet soil.
        const float bare = (1.0 - rock) * (1.0 - snow) * smoothstep(0.35, 0.1, slope);
        const float sandAmount = saturate(i.uv.x * 1.3) * bare;
        bankAmount = saturate(i.uv.y * 1.2) * (1.0 - rock * 0.7) * (1.0 - snow);
        const float grainShade = 0.7 + 0.6 * luma(g);
        baseColor = lerp(baseColor, float3(0.50, 0.42, 0.26) * grainShade, sandAmount);
        baseColor = lerp(baseColor, float3(0.06, 0.045, 0.03) * grainShade, bankAmount * 0.85);
    } else {
        // A second, larger tiling breaks the repetition of the near one.
        const float3 g = lerp(grassTex.Sample(materialSampler, i.uv).rgb, grassTex.Sample(materialSampler, i.uv * 0.13).rgb, 0.4);
        const float3 r = lerp(rockTex.Sample(materialSampler, i.uv * 0.7).rgb, rockTex.Sample(materialSampler, i.uv * 0.09).rgb, 0.4);
        const float3 s = snowTex.Sample(materialSampler, i.uv * 0.5).rgb;
        baseColor = g * grass + r * rock + s * snow;
    }
    // Under water the ground takes on the water's colour with depth, and the bank above it is wet.
    if (frame.waterColor.w > 0.0 && frame.cameraWorld.y > frame.waterParams.x) {
        const float below = frame.waterParams.x - height;
        const float wet = smoothstep(-1.0, 0.0, below) * 0.5;
        baseColor *= 1.0 - wet;
        baseColor = lerp(baseColor, frame.waterColor.rgb * 0.6, 1.0 - exp(-max(below, 0.0) * 0.35));
    }
    float roughness = lerp(0.95, 0.7, snow);
    roughness = lerp(roughness, 0.6, bankAmount);
    applyWetness(baseColor, roughness, 0.0);
    return toneMap(applyFog(shade(i.worldPos, n, n, baseColor, 0.0, roughness), i.worldPos, i.clip.xy));
}
