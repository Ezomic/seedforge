#ifndef ICHOR_COMMON_HLSLI
#define ICHOR_COMMON_HLSLI

#define CASCADE_COUNT 3

struct FrameData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4x4 cascadeViewProj[CASCADE_COUNT];
    float4 cascadeSplits;   // view-space depth where each cascade ends
    float4 sunDirection;    // xyz towards the sun
    float4 sunColor;        // rgb, w intensity
    float4 ambient;         // rgb
    float4 shadowParams;    // x texel size, y depth bias, z normal offset, w unused
    float4 cameraWorld;     // xyz world position of the camera, which is the origin of worldPos
    float4 skyZenith;       // rgb linear
    float4 skyHorizon;      // rgb linear
    float4 fogColor;        // rgb linear, w density per metre
    float4 skyParams;       // x tan(fov/2) * aspect, y tan(fov/2), z time in seconds, w rain intensity
    float4 wind;            // xy direction on the ground plane, z strength, w fog height falloff per metre
    float4 shaftParams;     // x light shaft strength (0 is off), y scattering anisotropy
    float4 waterColor;      // rgb colour of deep water, w 0 when there is no water
    float4 waterParams;     // x height of the surface in world metres
};

struct PushData {
    float4x4 model;
    float4 baseColorFactor;
    float4 params; // x metallic, y roughness, z has normal map, w cascade index (shadow pass)
    uint4 skin;    // x joint offset into the palette, y joint count (0 = rigid)
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(3, 0)]] StructuredBuffer<float4x4> jointPalette;
[[vk::push_constant]] PushData push;

// The camera is below the surface of the water.
bool isUnderwater()
{
    return frame.waterColor.w > 0.0 && frame.cameraWorld.y < frame.waterParams.x;
}

// What the water looks like from inside: its deep colour, lit by the sky above and a little sun.
float3 underwaterColor()
{
    return frame.waterColor.rgb * (0.3 + frame.ambient.rgb * 2.0 + frame.sunColor.rgb * frame.sunColor.w * 0.04);
}

float4 toneMap(float3 color)
{
    color = color / (color + 1.0);
    return float4(pow(color, 1.0 / 2.2), 1.0);
}

// How far the wind pushes a point of a tree sideways, on the ground plane. Bends more
// towards the crown, leans with the gusts that sweep across the land, and sways with a
// phase of its own. `height` is the vertex's height over the tree's foot before scaling,
// `anchor` the tree's position in its cell, which keeps the phase steady as the camera moves.
float2 windOffset(float height, float3 anchor, float scale)
{
    const float time = frame.skyParams.z;
    const float phase = anchor.x * 0.21 + anchor.z * 0.17;
    float bend = saturate(height / 7.0);
    bend *= bend;
    const float gust = 0.5 + 0.5 * sin(time * 0.35 + anchor.x * 0.01 + anchor.z * 0.013);
    const float sway = sin(time * 1.7 + phase) + 0.45 * sin(time * 3.3 + phase * 1.9);
    const float lean = 0.35 + 0.65 * gust;
    return frame.wind.xy * frame.wind.z * scale * bend * lean * (0.9 + 0.35 * sway);
}

// Blends the four joint matrices of a skinned vertex.
float4x4 skinMatrix(uint4 joints, float4 weights)
{
    float4x4 m = jointPalette[push.skin.x + joints.x] * weights.x;
    m += jointPalette[push.skin.x + joints.y] * weights.y;
    m += jointPalette[push.skin.x + joints.z] * weights.z;
    m += jointPalette[push.skin.x + joints.w] * weights.w;
    return m;
}

#endif
