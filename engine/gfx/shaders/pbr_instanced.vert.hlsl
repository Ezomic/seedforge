#include "common.hlsli"

struct VSIn {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float4 tangent : TANGENT;
    [[vk::location(3)]] float2 uv : TEXCOORD0;
    [[vk::location(6)]] float4 positionScale : TEXCOORD4; // xyz relative to the cell, w scale
    [[vk::location(7)]] float4 rotation : TEXCOORD5;      // cos(yaw), sin(yaw)
};

struct VSOut {
    float4 clip : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float rise : TEXCOORD4; // metres above the foot of the object, for blending it into the ground
};

// Turns about the vertical axis; the cell's translation comes from the push constants.
float3 yaw(float3 v, float c, float s)
{
    return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

VSOut main(VSIn i)
{
    const float c = i.rotation.x;
    const float s = i.rotation.y;
    const float3 cellOrigin = mul(push.model, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 world = yaw(i.position * i.positionScale.w, c, s) + i.positionScale.xyz + cellOrigin;
    world.xz += windOffset(i.position.y, i.positionScale.xyz, i.positionScale.w);

    VSOut o;
    o.clip = mul(frame.viewProj, float4(world, 1.0));
    o.worldPos = world;
    o.normal = normalize(yaw(i.normal, c, s));
    o.tangent = float4(normalize(yaw(i.tangent.xyz, c, s)), i.tangent.w);
    o.uv = i.uv;
    o.rise = i.position.y * i.positionScale.w;
    return o;
}
