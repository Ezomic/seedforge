#include "water.hlsli"

struct PSIn {
    float4 clip : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float rise : TEXCOORD4;
};

// Rivers and lakes: a surface mesh per cell, shaded like the sea. The mesh gives the position, so
// the rasteriser does the depth test against the land.
float4 main(PSIn i) : SV_Target
{
    // The current rides in the tangent: a direction on the ground plane, its strength in w.
    return shadeWater(i.worldPos, i.clip.xy, i.uv.x, i.tangent.xz * i.tangent.w, frame.waterParams.y > 0.5, i.uv.y);
}
