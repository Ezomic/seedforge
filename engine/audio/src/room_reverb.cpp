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

// Doubles hold any product of three finite floats, so a huge box never needs a sentinel.
RoomReverb fromDoubles(double volume, double surfaceArea, RoomMaterial material)
{
    const MaterialRow m = row(material);
    if (!(volume > 0.0) || !(surfaceArea > 0.0)) return {kRoomReverbMinDecay, 0.0f, m.damping};

    double decay = kSabine * volume / (surfaceArea * static_cast<double>(m.absorption));
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

}  // namespace

float roomMaterialAbsorption(RoomMaterial material)
{
    return row(material).absorption;
}

RoomReverb roomReverb(float volume, float surfaceArea, RoomMaterial material)
{
    if (!usable(volume) || !usable(surfaceArea)) return fromDoubles(0.0, 0.0, material);
    return fromDoubles(static_cast<double>(volume), static_cast<double>(surfaceArea), material);
}

RoomReverb roomReverbForBox(float width, float depth, float height, RoomMaterial material)
{
    if (!usable(width) || !usable(depth) || !usable(height)) return fromDoubles(0.0, 0.0, material);
    const double w = width;
    const double d = depth;
    const double h = height;
    return fromDoubles(w * d * h, 2.0 * (w * d + w * h + d * h), material);
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
