#ifndef ICHOR_COMMON_HLSLI
#define ICHOR_COMMON_HLSLI

#define CASCADE_COUNT 3

struct FrameData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4x4 cascadeViewProj[CASCADE_COUNT];
    float4 cascadeSplits;   // view-space depth where each cascade ends
    float4 sunDirection;    // xyz towards the sun
    float4 sunColor;        // rgb, w intensity
    float4 ambient;         // rgb
    float4 shadowParams;    // x texel size, y depth bias, z normal offset, w unused
};

struct PushData {
    float4x4 model;
    float4 baseColorFactor;
    float4 params; // x metallic, y roughness, z has normal map, w cascade index (shadow pass)
    uint4 skin;    // x joint offset into the palette, y joint count (0 = rigid)
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(3, 0)]] StructuredBuffer<float4x4> jointPalette;
[[vk::push_constant]] PushData push;

// Blends the four joint matrices of a skinned vertex.
float4x4 skinMatrix(uint4 joints, float4 weights)
{
    float4x4 m = jointPalette[push.skin.x + joints.x] * weights.x;
    m += jointPalette[push.skin.x + joints.y] * weights.y;
    m += jointPalette[push.skin.x + joints.z] * weights.z;
    m += jointPalette[push.skin.x + joints.w] * weights.w;
    return m;
}

#endif
