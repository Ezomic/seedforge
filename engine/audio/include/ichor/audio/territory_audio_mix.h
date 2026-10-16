#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// Doc 12 layer 6: you can hear whose land you are in. This is the pure-logic core only: from the
// listener position and the per-faction claim strengths, it picks how loud each faction's territory
// layer is. It reads plain integers (the layout of the faction flood-fill's strength layers) so the
// audio module does not depend on the factions module, and it holds no sim state.
constexpr std::uint32_t kTerritoryAudioMaxStrength = 1000;
constexpr std::size_t kTerritoryAudioMaxCells = 1u << 20;
constexpr std::size_t kTerritoryAudioMaxFactions = 16;

struct TerritoryAudioGrid {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // World units per cell (> 0). Cell (x, y) is centred at origin + (x + 0.5, y + 0.5) * cellSize.
    float cellSize = 1.0f;
    float originX = 0.0f;
    float originZ = 0.0f;
    // strength[faction][y * width + x], 0 = unclaimed, up to kTerritoryAudioMaxStrength. Contested
    // cells are simply cells where several factions are nonzero.
    std::vector<std::vector<std::uint16_t>> strength;
};

// One entry per faction, in the same order as TerritoryAudioGrid::strength.
struct TerritoryAudioLayer {
    // Empty = this faction has no territory sound (the Renouncers: nothing at all).
    std::string layerId;
    // Loudest this layer gets, 0..1.
    float maxGain = 1.0f;
};

struct TerritoryAudioMix {
    // Gain per faction layer (0 for silent layers). Same length as the layer list.
    std::vector<float> gains;
    // Faction with the largest gain, lowest index on ties; -1 when everything is silent.
    int dominant = -1;
};

// Bilinear sample of the claim strengths at (x, z), so every gain is continuous in position. The
// weight of faction f is s_f / max(1, sum of s), with s in 0..1: the weights sum to at most 1 (a
// contested cell splits the loudness between its claimants instead of adding to it), and fade to
// silence over unclaimed land. Positions off the grid hold the edge value. NaN or infinite
// positions, a malformed grid, or an unreadable faction layer give silence for that part, never a
// crash or a loud result. No libm: identical on every platform.
TerritoryAudioMix territoryAudioMix(const TerritoryAudioGrid& grid, const std::vector<TerritoryAudioLayer>& layers, float x, float z);

// Moves `current` toward `target` by at most dt / fadeSeconds per gain (a full 0..1 swing takes
// fadeSeconds), so a position jump or a fast player never pops. Different lengths, NaN or negative
// dt resolve safely: the result has target's length; bad dt leaves existing values unchanged;
// fadeSeconds <= 0 or NaN snaps to the target.
std::vector<float> territoryAudioStep(const std::vector<float>& current, const std::vector<float>& target, float dt, float fadeSeconds);

}  // namespace ichor
