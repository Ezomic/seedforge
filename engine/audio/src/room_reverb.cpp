#include "ichor/audio/room_reverb.h"

#include <cmath>

namespace ichor {

namespace {

struct MaterialRow {
    float absorption;
    float damping;
};

MaterialRow row(RoomMaterial material)
{
    switch (material) {
    case RoomMaterial::Stone: return {0.03f, 0.20f};
    case RoomMaterial::Brick: return {0.04f, 0.30f};
    case RoomMaterial::Metal: return {0.05f, 0.10f};
    case RoomMaterial::Timber: return {0.10f, 0.50f};
    case RoomMaterial::Earth: return {0.25f, 0.70f};
    case RoomMaterial::Cloth: return {0.50f, 0.90f};
    }
    return {0.03f, 0.20f};
}

bool usable(float x)
{
    return std::isfinite(x) && x > 0.0f;
}

constexpr double kSabine = 0.161;
constexpr double kWetFloor = 0.08;
constexpr double kWetSpan = 0.52;
constexpr double kWetFullAtDecay = 8.0;

}  // namespace

float roomMaterialAbsorption(RoomMaterial material)
{
    return row(material).absorption;
}

RoomReverb roomReverb(float volume, float surfaceArea, RoomMaterial material)
{
    const MaterialRow m = row(material);
    if (!usable(volume) || !usable(surfaceArea)) return {kRoomReverbMinDecay, 0.0f, m.damping};

    const double absorbing = static_cast<double>(surfaceArea) * static_cast<double>(m.absorption);
    double decay = kSabine * static_cast<double>(volume) / absorbing;
    if (!(decay > kRoomReverbMinDecay)) decay = kRoomReverbMinDecay;
    if (decay > kRoomReverbMaxDecay) decay = kRoomReverbMaxDecay;

    double reach = (decay - kRoomReverbMinDecay) / (kWetFullAtDecay - kRoomReverbMinDecay);
    if (reach > 1.0) reach = 1.0;

    RoomReverb out;
    out.decaySeconds = static_cast<float>(decay);
    out.wet = static_cast<float>(kWetFloor + kWetSpan * reach);
    out.damping = m.damping;
    return out;
}

RoomReverb roomReverbForBox(float width, float depth, float height, RoomMaterial material)
{
    if (!usable(width) || !usable(depth) || !usable(height)) return roomReverb(0.0f, 0.0f, material);
    const double w = width;
    const double d = depth;
    const double h = height;
    const double volume = w * d * h;
    const double area = 2.0 * (w * d + w * h + d * h);
    constexpr double kFloatMax = 3.0e38;
    if (volume > kFloatMax || area > kFloatMax) {
        // Out of float range: a vast room. Keep the volume to area ratio from collapsing.
        return roomReverb(3.0e38f, 3.0e30f, material);
    }
    return roomReverb(static_cast<float>(volume), static_cast<float>(area), material);
}

ReverbSettings toReverbSettings(const RoomReverb& room)
{
    ReverbSettings s;
    s.wet = room.wet;
    s.damping = room.damping;
    s.roomSize = (room.decaySeconds - kRoomReverbMinDecay) / (kRoomReverbMaxDecay - kRoomReverbMinDecay);
    return s;
}

}  // namespace ichor
