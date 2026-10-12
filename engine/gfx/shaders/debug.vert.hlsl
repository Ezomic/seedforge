#include "common.hlsli"

struct VSIn {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR0;
};

struct VSOut {
    float4 clip : SV_Position;
    float3 color : COLOR0;
};

VSOut main(VSIn i)
{
    VSOut o;
    o.clip = mul(frame.viewProj, float4(i.position, 1.0));
    o.color = i.color;
    return o;
}
