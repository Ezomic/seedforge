#include "common.hlsli"

struct VSIn {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(4)]] uint4 joints : BLENDINDICES;
    [[vk::location(5)]] float4 weights : BLENDWEIGHT;
};

float4 main(VSIn i) : SV_Position
{
    const float4x4 model = mul(push.model, skinMatrix(i.joints, i.weights));
    const float4 world = mul(model, float4(i.position, 1.0));
    return mul(frame.cascadeViewProj[(int)push.params.w], world);
}
