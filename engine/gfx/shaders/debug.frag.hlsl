float4 main(float4 clip : SV_Position, float3 color : COLOR0) : SV_Target
{
    return float4(pow(color, 1.0 / 2.2), 1.0);
}
