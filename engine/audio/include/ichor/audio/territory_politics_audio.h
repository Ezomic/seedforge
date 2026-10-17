#pragma once

#include "ichor/audio/territory_audio_mix.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ichor {

// SEED-61, doc 12 layer 6: the politics map (owner and rival per cell, as PoliticsMap lays them
// out) becomes the strength grid that territoryAudioMix reads, and a small tracker keeps the
// dominant territory from flapping while the listener stands on a border. Plain integers in and
// out, so the audio module does not depend on libworldforge. Pure logic: no playback, no assets.
constexpr std::uint8_t kTerritoryAudioNoOwner = 255;
constexpr int kTerritoryAudioMaxBleed = 4;

struct TerritoryPoliticsParams {
    // A faction's sound leaks this many cells (Manhattan) into neighbouring land, fading linearly,
    // so a border is heard before it is seen. In [0, kTerritoryAudioMaxBleed].
    int bleedCells = 2;
    // Strengths, each in [0, kTerritoryAudioMaxStrength]. A contested cell splits the loudness.
    std::uint16_t ownerStrength = 1000;
    std::uint16_t contestedOwnerStrength = 600;
    std::uint16_t contestedRivalStrength = 400;
};

// owner and rival are width * height (row-major); values are below factionCount or
// kTerritoryAudioNoOwner. Returns nullopt for a zero or oversized grid, factionCount outside
// [1, kTerritoryAudioMaxFactions], mismatched spans, an owner or rival id that is neither a faction
// nor "none", a rival on an unowned cell or equal to its owner, a non-finite or non-positive
// cellSize, a non-finite origin, or parameters out of range. Integer-only and deterministic.
std::optional<TerritoryAudioGrid> territoryGridFromPolitics(std::span<const std::uint8_t> owner, std::span<const std::uint8_t> rival,
    std::uint32_t width, std::uint32_t height, int factionCount, float cellSize, float originX, float originZ, const TerritoryPoliticsParams& params = {});

struct TerritoryHysteresis {
    // A new faction must lead the held one by this much gain to take over, in [0, 1].
    float switchMargin = 0.15f;
    // The held faction is released to "none" only once its gain falls below this, in [0, 1].
    float releaseBelow = 0.05f;
    // A faction is acquired from "none" only once its gain reaches this, in [releaseBelow, 1].
    float acquireAbove = 0.2f;
};

struct TerritoryTracker {
    int held = -1;
};

struct TerritoryTrackResult {
    int dominant = -1;
    // True on the call where `dominant` changed, for a stinger or a cue.
    bool changed = false;
};

// Updates the tracker from a mix. Dominance changes only across the margins above, so a listener
// jittering over a border keeps one dominant layer. Gains are not altered. A tracker holding an
// index outside the mix, NaN gains or out-of-range hysteresis values resolve safely (hysteresis is
// clamped, NaN gains count as silent).
TerritoryTrackResult territoryTrack(TerritoryTracker& tracker, const TerritoryAudioMix& mix, const TerritoryHysteresis& h = {});

}  // namespace ichor
