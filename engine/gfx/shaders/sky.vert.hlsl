#include "common.hlsli"

struct VSOut {
    float4 clip : SV_Position;
    float3 direction : TEXCOORD0;
};

// One triangle that covers the screen. The scene pass draws with a flipped
// viewport, so clip y up is screen up. The view ray is rotated back into world
// space with the transpose of the view matrix's rotation.
VSOut main(uint id : SV_VertexID)
{
    const float2 ndc = float2((id << 1) & 2, id & 2) * 2.0 - 1.0;
    const float3 viewRay = float3(ndc.x * frame.skyParams.x, ndc.y * frame.skyParams.y, -1.0);
    VSOut o;
    o.clip = float4(ndc, 1.0, 1.0);
    o.direction = mul(viewRay, (float3x3)frame.view);
    return o;
}
