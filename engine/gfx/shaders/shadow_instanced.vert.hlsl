#include "common.hlsli"

struct VSIn {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(6)]] float4 positionScale : TEXCOORD4; // xyz relative to the cell, w scale
    [[vk::location(7)]] float4 rotation : TEXCOORD5;      // cos(yaw), sin(yaw)
};

// The same placement as pbr_instanced.vert, into the cascade named by the push constants.
float4 main(VSIn i) : SV_Position
{
    const float c = i.rotation.x;
    const float s = i.rotation.y;
    const float3 local = i.position * i.positionScale.w;
    const float3 turned = float3(c * local.x + s * local.z, local.y, -s * local.x + c * local.z);
    const float3 cellOrigin = mul(push.model, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 world = turned + i.positionScale.xyz + cellOrigin;
    world.xz += windOffset(i.position.y, i.positionScale.xyz, i.positionScale.w);
    return mul(frame.cascadeViewProj[(int)push.params.w], float4(world, 1.0));
}
