[[vk::binding(0, 0)]] Texture2D uiTexture;
[[vk::binding(1, 0)]] SamplerState uiSampler;

struct PSIn {
    float4 clip : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// Vertex colours and textures are both premultiplied by alpha, so the product is too.
float4 main(PSIn i) : SV_Target
{
    return uiTexture.Sample(uiSampler, i.uv) * i.color;
}
