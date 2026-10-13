#include "water.hlsli"

struct PSIn {
    float4 clip : SV_Position;
    float3 direction : TEXCOORD0;
};

struct PSOut {
    float4 color : SV_Target;
    float depth : SV_Depth;
};

// The sea: one sheet at one height, found by intersecting each view ray with its plane. The
// depth is written from that point, so land in front of the water hides it.
PSOut main(PSIn i)
{
    const float level = frame.waterParams.x;
    const float height = level - frame.cameraWorld.y;
    const float3 direction = normalize(i.direction);
    // Only looking down at the water from above it.
    if (frame.waterColor.w <= 0.0 || height >= 0.0 || direction.y >= -1e-4) {
        discard;
    }
    const float3 position = direction * (height / direction.y);
    const float4 clip = mul(frame.viewProj, float4(position, 1.0));
    // A little nearer than exact, so the waterline does not speckle where it meets the land.
    const float depth = clip.z / clip.w - 2e-6;
    if (depth >= 1.0) {
        discard;
    }

    PSOut o;
    o.color = shadeWater(position, i.clip.xy);
    o.depth = depth;
    return o;
}
