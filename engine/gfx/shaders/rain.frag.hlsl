#include "common.hlsli"

struct PSIn {
    float4 clip : SV_Position;
    float3 direction : TEXCOORD0;
};

float hash(float n)
{
    return frac(sin(n * 12.9898) * 43758.5453);
}

// One depth of rain: thin vertical streaks in columns, each falling at its own speed.
float streaks(float2 pixel, float time, float layer)
{
    const float scale = 1.0 + layer * 0.7;
    const float width = 3.0 * scale;
    const float column = floor(pixel.x / width);
    const float h = hash(column + layer * 91.7);
    const float fall = (900.0 + 500.0 * h) / scale;
    const float y = pixel.y / scale + time * fall + h * 1000.0;
    const float along = frac(y / 70.0);
    const float body = smoothstep(0.0, 0.1, along) * (1.0 - smoothstep(0.3, 0.45, along));
    const float across = frac(pixel.x / width);
    const float stripe = smoothstep(0.3, 0.5, across) * (1.0 - smoothstep(0.5, 0.7, across));
    return body * stripe * (0.35 + 0.65 * h);
}

// A screen-space curtain of rain over the finished picture, thicker with the downpour.
float4 main(PSIn i) : SV_Target
{
    const float time = frame.skyParams.z;
    const float rain = frame.skyParams.w;
    const float density = streaks(i.clip.xy, time, 0.0) + streaks(i.clip.xy, time, 1.0) * 0.8 + streaks(i.clip.xy, time, 2.0) * 0.6;
    // Linear light, tone mapped like everything else would be, but already display-ready: a pale blue-grey.
    const float alpha = saturate(density * rain * 0.3);
    return float4(0.72, 0.78, 0.88, alpha);
}
