#pragma once

#include "ichor/audio/reverb.h"

#include <cstdint>

namespace ichor {

// The dominant surface of a space, as the prefab kit knows it.
enum class RoomMaterial : std::uint8_t {
    Stone,
    Brick,
    Metal,
    Timber,
    Earth,
    Cloth,
};

// What the forge stamps onto a generated space (docs/12-audio-atmosphere.md): a lookup from
// computed volume and dominant material, not a simulation. Pure arithmetic on IEEE doubles with
// no libm, so it is identical on every platform.
struct RoomReverb {
    float decaySeconds = 0.0f; // RT60, clamped to kRoomReverbMinDecay..kRoomReverbMaxDecay
    float wet = 0.0f;          // 0 dry, 1 very wet
    float damping = 0.0f;      // 0 bright tail, 1 muffled tail

    bool operator==(const RoomReverb&) const = default;
};

constexpr float kRoomReverbMinDecay = 0.1f;
constexpr float kRoomReverbMaxDecay = 12.0f;

// Mean absorption coefficient of the material, 0..1. An unknown value counts as stone.
float roomMaterialAbsorption(RoomMaterial material);

// Sabine estimate: RT60 = 0.161 * volume / (surfaceArea * absorption), in metres. Volume and
// surface area must be finite and positive; anything else (zero, negative, NaN, infinity) is a bad
// reading and yields the dry preset (minimum decay, no wet) rather than a guess. Finite extremes
// are clamped. Never decays faster for a larger volume, nor slower for a larger surface area.
RoomReverb roomReverb(float volume, float surfaceArea, RoomMaterial material);

// The same for an axis-aligned box of the given width, depth and height in metres.
RoomReverb roomReverbForBox(float width, float depth, float height, RoomMaterial material);

// The preset in the form the Reverb filter takes.
ReverbSettings toReverbSettings(const RoomReverb& room);

}  // namespace ichor
