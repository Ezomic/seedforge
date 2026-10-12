#include "common.hlsli"

struct VSIn {
    [[vk::location(0)]] float3 position : POSITION;
};

float4 main(VSIn i) : SV_Position
{
    const float4 world = mul(push.model, float4(i.position, 1.0));
    return mul(frame.cascadeViewProj[(int)push.params.w], world);
}
