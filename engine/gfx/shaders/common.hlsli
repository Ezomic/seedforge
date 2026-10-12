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
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::push_constant]] PushData push;

#endif
