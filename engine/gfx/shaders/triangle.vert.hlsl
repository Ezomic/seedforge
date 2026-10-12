struct VSOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VSOut main(uint id : SV_VertexID)
{
    const float2 positions[3] = { float2(0.0, -0.5), float2(0.5, 0.5), float2(-0.5, 0.5) };
    const float3 colors[3] = { float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0) };

    VSOut o;
    o.position = float4(positions[id], 0.0, 1.0);
    o.color = colors[id];
    return o;
}
