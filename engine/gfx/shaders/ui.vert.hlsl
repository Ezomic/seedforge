struct Push {
    float2 translation; // pixels
    float2 viewport;    // pixels
};
[[vk::push_constant]] Push push;

struct VSIn {
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct VSOut {
    float4 clip : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

VSOut main(VSIn i)
{
    VSOut o;
    // Pixels with the origin top-left to clip space; the overlay pass uses an upright viewport.
    const float2 pixel = i.position + push.translation;
    o.clip = float4(pixel / push.viewport * 2.0 - 1.0, 0.0, 1.0);
    o.color = i.color;
    o.uv = i.uv;
    return o;
}
