#include "common.hlsli"

struct VSIn {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float4 tangent : TANGENT;
    [[vk::location(3)]] float2 uv : TEXCOORD0;
};

struct VSOut {
    float4 clip : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float rise : TEXCOORD4; // metres above the foot of the object, for blending it into the ground
};

VSOut main(VSIn i)
{
    const float4 world = mul(push.model, float4(i.position, 1.0));
    const float3x3 basis = (float3x3)push.model;

    VSOut o;
    o.clip = mul(frame.viewProj, world);
    o.worldPos = world.xyz;
    o.normal = normalize(mul(basis, i.normal));
    o.tangent = float4(normalize(mul(basis, i.tangent.xyz)), i.tangent.w);
    o.uv = i.uv;
    o.rise = world.y - push.model[3].y;
    return o;
}
