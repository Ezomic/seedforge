#pragma once

#include <worldforge/climate_map.h>
#include <worldforge/hydrology_map.h>
#include <worldforge/subseed.h>
#include <worldforge/tectonics_map.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <vector>

// Phase 5, Biomes (doc 08): a Whittaker-style temperature x moisture lookup, modulated by slope and
// elevation, then axiom injection and suppression. Later phases (settlements, roads, politics,
// POIs, bake) consume BiomeMap; none of them may mutate it. The inputs are only read.
//
// Output contract (BiomeMap, same grid as the inputs, row-major, index = y * width + x):
//  - biome[c]     : BiomeId of the cell, always < kBiomeCount.
//  - secondary[c] : the BiomeId most common among the cell's differing 4-neighbours (ties: lowest
//                   id), or biome[c] itself when no neighbour differs.
//  - blend[c]     : 0 to 255, the share of the cell's in-map 4-neighbours that carry a different
//                   biome (255 = all of them). Interior cells of a uniform region read 0, so a
//                   consumer blends biome[c] and secondary[c] with weight blend[c] / 255.
// Ocean and Lake are never injected over or suppressed. Crag is the terminal fallback of every
// suppression chain and cannot be suppressed, so a suppressed biome never appears in the output.
//
// Algorithm, per cell:
//  1. Water: lakeDepthM > 0 gives Lake; final height <= seaLevelM gives Ocean. Everything below
//     applies to land only. seaLevelM must equal the HydrologyParams::seaLevelM used for phase 3.
//  2. Slope is the largest absolute height difference to a land 4-neighbour, in metres. At or above
//     cragSlopeM the cell is Crag (bare rock).
//  3. Temperature (ClimateMap::temperatureC10, already lapse-cooled) picks one of six rows by the
//     five ascending tempEdgesC10 (below the first: Glacier). Moisture (ClimateMap::moisture) picks
//     one of four columns by the three ascending moistureEdges. Land moisture is rain per cell, so
//     its values are small: the default edges (2, 5, 12) are tuned to generateClimate's default
//     ClimateParams, and a climate built with other rain parameters wants other edges. The remaining five rows read:
//         tundra : Tundra  Tundra  Tundra     Tundra
//         cold   : Tundra  Taiga   Taiga      Taiga
//         cool   : Grassland Forest Forest    Forest
//         warm   : Desert  Grassland Forest   Forest
//         hot    : Desert  Savanna Savanna    Rainforest
//  4. Elevation: at or above alpineM a non-Glacier cell becomes Alpine.
//  5. Wet lowland: a cell at or below marshMaxHeightM, slope at most marshMaxSlopeM, in the wettest
//     moisture column, not Glacier or Alpine, within one cell (8-neighbourhood) of a river or lake,
//     becomes Marsh.
//  6. Farmland: a Grassland cell at or below farmMaxHeightM with slope at most farmMaxSlopeM becomes
//     LowlandFarm.
//  7. Injection, in list order (later entries overwrite earlier ones). An entry replaces land cells
//     whose height lies in [minHeightM, maxHeightM] and, when coastReach > 0, whose 4-neighbour
//     distance to the nearest Ocean cell is at most coastReach. Of those, a patchCells x patchCells
//     block is chosen when a hash of (seed, entry index, block) & 255 is below coverage (256 = every
//     block). Entries whose biome is suppressed are skipped.
//  8. Suppression: a cell whose biome is in suppressMask is moved down the fallback chain
//     (LowlandFarm to Grassland to Forest to Taiga to Tundra to Crag; Desert to Savanna to
//     Grassland; Marsh to Grassland; Rainforest to Forest; Glacier to Tundra; Alpine to Crag;
//     SaltBlight to Desert; TidalRuin to Marsh) until it reaches an unsuppressed biome.
//
// Determinism: integer arithmetic only, no libm, no floating point, no unordered container; the
// only randomness is hashing via subseed.h. Memory is O(cells), about 12 bytes per cell of working
// state (an extra 2 bytes per cell only when an injection asks for a coast), cells <= kTectonicsMaxSize^2.
namespace worldforge {

enum class BiomeId : std::uint8_t {
    Ocean,
    Lake,
    Glacier,
    Tundra,
    Taiga,
    Forest,
    Grassland,
    Desert,
    Savanna,
    Rainforest,
    Alpine,
    Crag,
    Marsh,
    LowlandFarm,
    SaltBlight,
    TidalRuin,
};

constexpr int kBiomeCount = 16;
constexpr int kBiomeMaxInjections = 16;
constexpr int kBiomeMaxPatchCells = 64;
constexpr int kBiomeMaxCoastReach = 255;
constexpr int kBiomeMaxCoverage = 256;
constexpr int kBiomeTempEdges = 5;
constexpr int kBiomeMoistureEdges = 3;
constexpr int kBiomeMaxSlopeM = kTectonicsMaxHeightM - kTectonicsMinHeightM;

constexpr std::array<std::string_view, kBiomeCount> kBiomeNames = {
    "ocean", "lake", "glacier", "tundra", "taiga", "forest", "grassland", "desert",
    "savanna", "rainforest", "alpine", "crag", "marsh", "lowland_farm", "salt_blight", "tidal_ruin",
};

// Maps an axiom directive id ("salt_blight") to its BiomeId; nullopt for an unknown id.
constexpr std::optional<BiomeId> biomeFromName(std::string_view name)
{
    for (int i = 0; i < kBiomeCount; ++i) {
        if (kBiomeNames[static_cast<std::size_t>(i)] == name) return static_cast<BiomeId>(i);
    }
    return std::nullopt;
}

constexpr std::uint32_t biomeBit(BiomeId id) { return std::uint32_t{ 1 } << static_cast<unsigned>(id); }

// Ocean, Lake and Crag cannot be suppressed.
constexpr std::uint32_t kBiomeSuppressible = ((std::uint32_t{ 1 } << kBiomeCount) - 1) & ~(biomeBit(BiomeId::Ocean) | biomeBit(BiomeId::Lake) | biomeBit(BiomeId::Crag));

struct BiomeInjection {
    BiomeId biome = BiomeId::SaltBlight; // any land biome, not Ocean or Lake
    int coverage = 0;                    // share of eligible blocks replaced, out of 256, in [0, 256]
    int minHeightM = kTectonicsMinHeightM; // eligible height range, inclusive, minHeightM <= maxHeightM
    int maxHeightM = kTectonicsMaxHeightM;
    int coastReach = 0;                  // 0 = anywhere, else only land within this many cells of the sea, in [0, 255]
    int patchCells = 4;                  // side of a block chosen as a unit, in [1, 64]
};

struct BiomeParams {
    int seaLevelM = 0;                                           // must match HydrologyParams::seaLevelM
    std::array<int, kBiomeTempEdges> tempEdgesC10 = { -150, -30, 100, 200, 270 }; // ascending, in [-600, 600]
    std::array<int, kBiomeMoistureEdges> moistureEdges = { 2, 5, 12 };           // ascending, in [0, 1024], in ClimateMap::moisture units
    int alpineM = 2200;        // elevation from which land is Alpine, in the tectonics height range
    int cragSlopeM = 90;       // slope (metres per cell) from which land is Crag, in [1, kBiomeMaxSlopeM]
    int marshMaxHeightM = 60;  // in the tectonics height range
    int marshMaxSlopeM = 40;   // in [0, kBiomeMaxSlopeM]
    int farmMaxHeightM = 500;  // in the tectonics height range
    int farmMaxSlopeM = 25;    // in [0, kBiomeMaxSlopeM]
    std::uint32_t suppressMask = 0; // bit per BiomeId, only kBiomeSuppressible bits allowed
    std::vector<BiomeInjection> injections; // at most kBiomeMaxInjections
};

struct BiomeInputs {
    const ClimateMap* climate = nullptr;
    const HydrologyMap* hydrology = nullptr;
};

struct BiomeMap {
    std::uint64_t seed = 0; // seed of the inputs
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> biome;
    std::vector<std::uint8_t> secondary;
    std::vector<std::uint8_t> blend;

    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); }
};

namespace detail {

constexpr std::uint64_t kBiomeDomain = 0x62696f6d65733035ull;

constexpr BiomeId biomeFallback(BiomeId id)
{
    switch (id) {
    case BiomeId::LowlandFarm: return BiomeId::Grassland;
    case BiomeId::Grassland: return BiomeId::Forest;
    case BiomeId::Forest: return BiomeId::Taiga;
    case BiomeId::Taiga: return BiomeId::Tundra;
    case BiomeId::Desert: return BiomeId::Savanna;
    case BiomeId::Savanna: return BiomeId::Grassland;
    case BiomeId::Marsh: return BiomeId::Grassland;
    case BiomeId::Rainforest: return BiomeId::Forest;
    case BiomeId::Glacier: return BiomeId::Tundra;
    case BiomeId::SaltBlight: return BiomeId::Desert;
    case BiomeId::TidalRuin: return BiomeId::Marsh;
    default: return BiomeId::Crag;
    }
}

constexpr BiomeId kBiomeWhittaker[kBiomeTempEdges][kBiomeMoistureEdges + 1] = {
    { BiomeId::Tundra, BiomeId::Tundra, BiomeId::Tundra, BiomeId::Tundra },
    { BiomeId::Tundra, BiomeId::Taiga, BiomeId::Taiga, BiomeId::Taiga },
    { BiomeId::Grassland, BiomeId::Forest, BiomeId::Forest, BiomeId::Forest },
    { BiomeId::Desert, BiomeId::Grassland, BiomeId::Forest, BiomeId::Forest },
    { BiomeId::Desert, BiomeId::Savanna, BiomeId::Savanna, BiomeId::Rainforest },
};

inline bool biomeInputValid(const BiomeInputs& in)
{
    if (in.climate == nullptr || in.hydrology == nullptr) return false;
    const ClimateMap& c = *in.climate;
    const HydrologyMap& h = *in.hydrology;
    if (c.width < kTectonicsMinSize || c.width > kTectonicsMaxSize || c.height < kTectonicsMinSize || c.height > kTectonicsMaxSize) return false;
    if (h.width != c.width || h.height != c.height || h.seed != c.seed) return false;
    const std::size_t cells = static_cast<std::size_t>(c.width) * static_cast<std::size_t>(c.height);
    if (c.temperatureC10.size() != cells || c.moisture.size() != cells) return false;
    if (h.heightM.size() != cells || h.lakeDepthM.size() != cells || h.river.size() != cells) return false;
    const auto inHeight = [](std::int16_t v) { return v >= kTectonicsMinHeightM && v <= kTectonicsMaxHeightM; };
    return std::all_of(c.moisture.begin(), c.moisture.end(), [](std::uint16_t v) { return v <= kClimateMaxMoisture; }) && std::all_of(h.heightM.begin(), h.heightM.end(), inHeight);
}

inline bool biomeParamsValid(const BiomeParams& p)
{
    const auto inRange = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
    const auto heightOk = [&](int v) { return inRange(v, kTectonicsMinHeightM, kTectonicsMaxHeightM); };
    if (!heightOk(p.seaLevelM) || !heightOk(p.alpineM) || !heightOk(p.marshMaxHeightM) || !heightOk(p.farmMaxHeightM)) return false;
    if (!inRange(p.cragSlopeM, 1, kBiomeMaxSlopeM) || !inRange(p.marshMaxSlopeM, 0, kBiomeMaxSlopeM) || !inRange(p.farmMaxSlopeM, 0, kBiomeMaxSlopeM)) return false;
    for (std::size_t i = 0; i < p.tempEdgesC10.size(); ++i) {
        if (!inRange(p.tempEdgesC10[i], kClimateMinTempC10, kClimateMaxTempC10) || (i > 0 && p.tempEdgesC10[i] < p.tempEdgesC10[i - 1])) return false;
    }
    for (std::size_t i = 0; i < p.moistureEdges.size(); ++i) {
        if (!inRange(p.moistureEdges[i], 0, kClimateMaxMoisture) || (i > 0 && p.moistureEdges[i] < p.moistureEdges[i - 1])) return false;
    }
    if ((p.suppressMask & ~kBiomeSuppressible) != 0) return false;
    if (p.injections.size() > static_cast<std::size_t>(kBiomeMaxInjections)) return false;
    return std::all_of(p.injections.begin(), p.injections.end(), [&](const BiomeInjection& i) {
        const int id = static_cast<int>(i.biome);
        return id >= static_cast<int>(BiomeId::Glacier) && id < kBiomeCount && inRange(i.coverage, 0, kBiomeMaxCoverage) && heightOk(i.minHeightM) && heightOk(i.maxHeightM)
            && i.minHeightM <= i.maxHeightM && inRange(i.coastReach, 0, kBiomeMaxCoastReach) && inRange(i.patchCells, 1, kBiomeMaxPatchCells);
    });
}

constexpr std::uint16_t kBiomeFar = 0xFFFF;

// 4-neighbour breadth-first distance in cells from the nearest ocean cell, stopping at `reach`.
inline std::vector<std::uint16_t> oceanDistance(const std::vector<std::uint8_t>& biome, int w, int h, int reach)
{
    const std::size_t cells = biome.size();
    std::vector<std::uint16_t> dist(cells, kBiomeFar);
    std::vector<std::uint32_t> queue;
    queue.reserve(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        if (biome[c] == static_cast<std::uint8_t>(BiomeId::Ocean)) {
            dist[c] = 0;
            queue.push_back(static_cast<std::uint32_t>(c));
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::uint32_t c = queue[head];
        const int d = dist[c];
        if (d >= reach) continue;
        const int x = static_cast<int>(c % static_cast<std::uint32_t>(w));
        const int y = static_cast<int>(c / static_cast<std::uint32_t>(w));
        for (int n = 0; n < 4; ++n) {
            const int nx = x + (n == 0 ? 1 : n == 1 ? -1 : 0);
            const int ny = y + (n == 2 ? 1 : n == 3 ? -1 : 0);
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const std::size_t nc = static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx);
            if (dist[nc] != kBiomeFar) continue;
            dist[nc] = static_cast<std::uint16_t>(d + 1);
            queue.push_back(static_cast<std::uint32_t>(nc));
        }
    }
    return dist;
}

// Largest height difference to a land 4-neighbour: a sea floor or lake bed is not a cliff face.
inline int biomeSlope(const HydrologyMap& hy, int seaLevelM, int x, int y)
{
    const int w = hy.width;
    const int v = hy.heightM[hy.index(x, y)];
    int slope = 0;
    for (int n = 0; n < 4; ++n) {
        const int nx = x + (n == 0 ? 1 : n == 1 ? -1 : 0);
        const int ny = y + (n == 2 ? 1 : n == 3 ? -1 : 0);
        if (nx < 0 || ny < 0 || nx >= w || ny >= hy.height) continue;
        const std::size_t c = hy.index(nx, ny);
        if (hy.lakeDepthM[c] > 0 || hy.heightM[c] <= seaLevelM) continue;
        slope = std::max(slope, std::abs(v - hy.heightM[c]));
    }
    return slope;
}

inline bool nearWater(const HydrologyMap& hy, int x, int y)
{
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= hy.width || ny >= hy.height) continue;
            const std::size_t n = hy.index(nx, ny);
            if (hy.river[n] != 0 || hy.lakeDepthM[n] > 0) return true;
        }
    }
    return false;
}

inline void classifyBiomes(const BiomeInputs& in, const BiomeParams& p, BiomeMap& out)
{
    const ClimateMap& cl = *in.climate;
    const HydrologyMap& hy = *in.hydrology;
    const int w = out.width;
    const int h = out.height;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t c = out.index(x, y);
            const int z = hy.heightM[c];
            BiomeId b;
            if (hy.lakeDepthM[c] > 0) {
                b = BiomeId::Lake;
            } else if (z <= p.seaLevelM) {
                b = BiomeId::Ocean;
            } else {
                const int slope = biomeSlope(hy, p.seaLevelM, x, y);
                const int t = cl.temperatureC10[c];
                const int m = cl.moisture[c];
                int row = 0;
                while (row < kBiomeTempEdges && t >= p.tempEdgesC10[static_cast<std::size_t>(row)]) ++row;
                int col = 0;
                while (col < kBiomeMoistureEdges && m >= p.moistureEdges[static_cast<std::size_t>(col)]) ++col;
                b = row == 0 ? BiomeId::Glacier : kBiomeWhittaker[row - 1][col];
                if (slope >= p.cragSlopeM) {
                    b = BiomeId::Crag;
                } else {
                    if (z >= p.alpineM && b != BiomeId::Glacier) b = BiomeId::Alpine;
                    if (col == kBiomeMoistureEdges && z <= p.marshMaxHeightM && slope <= p.marshMaxSlopeM && b != BiomeId::Glacier && b != BiomeId::Alpine && nearWater(hy, x, y)) {
                        b = BiomeId::Marsh;
                    }
                    if (b == BiomeId::Grassland && z <= p.farmMaxHeightM && slope <= p.farmMaxSlopeM) b = BiomeId::LowlandFarm;
                }
            }
            out.biome[c] = static_cast<std::uint8_t>(b);
        }
    }
}

inline void injectBiomes(const HydrologyMap& hy, const BiomeParams& p, BiomeMap& out)
{
    int maxReach = 0;
    for (const BiomeInjection& inj : p.injections) maxReach = std::max(maxReach, inj.coastReach);
    std::vector<std::uint16_t> coast;
    if (maxReach > 0) coast = oceanDistance(out.biome, out.width, out.height, maxReach);

    for (std::size_t k = 0; k < p.injections.size(); ++k) {
        const BiomeInjection& inj = p.injections[k];
        if ((p.suppressMask & biomeBit(inj.biome)) != 0 || inj.coverage == 0) continue;
        const std::uint64_t salt = splitmix64(splitmix64(out.seed ^ kBiomeDomain) ^ static_cast<std::uint64_t>(k));
        for (int y = 0; y < out.height; ++y) {
            for (int x = 0; x < out.width; ++x) {
                const std::size_t c = out.index(x, y);
                const std::uint8_t current = out.biome[c];
                if (current == static_cast<std::uint8_t>(BiomeId::Ocean) || current == static_cast<std::uint8_t>(BiomeId::Lake)) continue;
                const int z = hy.heightM[c];
                if (z < inj.minHeightM || z > inj.maxHeightM) continue;
                if (inj.coastReach > 0 && coast[c] > inj.coastReach) continue;
                const std::uint64_t pick = splitmix64(salt ^ hashCell(x / inj.patchCells, y / inj.patchCells)) & 255u;
                if (static_cast<int>(pick) < inj.coverage) out.biome[c] = static_cast<std::uint8_t>(inj.biome);
            }
        }
    }
}

inline void suppressBiomes(const BiomeParams& p, BiomeMap& out)
{
    if (p.suppressMask == 0) return;
    for (std::uint8_t& v : out.biome) {
        BiomeId b = static_cast<BiomeId>(v);
        while ((p.suppressMask & biomeBit(b)) != 0) b = biomeFallback(b);
        v = static_cast<std::uint8_t>(b);
    }
}

inline void blendBiomes(BiomeMap& out)
{
    const int w = out.width;
    const int h = out.height;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t c = out.index(x, y);
            const std::uint8_t own = out.biome[c];
            int neighbours = 0;
            int differing = 0;
            std::array<int, kBiomeCount> votes{};
            for (int n = 0; n < 4; ++n) {
                const int nx = x + (n == 0 ? 1 : n == 1 ? -1 : 0);
                const int ny = y + (n == 2 ? 1 : n == 3 ? -1 : 0);
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                ++neighbours;
                const std::uint8_t other = out.biome[out.index(nx, ny)];
                if (other == own) continue;
                ++differing;
                ++votes[other];
            }
            int best = own;
            int bestVotes = 0;
            for (int b = 0; b < kBiomeCount; ++b) {
                if (votes[static_cast<std::size_t>(b)] > bestVotes) {
                    best = b;
                    bestVotes = votes[static_cast<std::size_t>(b)];
                }
            }
            out.secondary[c] = static_cast<std::uint8_t>(best);
            out.blend[c] = static_cast<std::uint8_t>(neighbours == 0 ? 0 : differing * 255 / neighbours);
        }
    }
}

}

// Returns nullopt when an input is missing or malformed (null maps, size outside the kTectonics*
// limits, vectors of the wrong length, moisture above kClimateMaxMoisture, heights out of range,
// climate and hydrology from different seeds or grids) or a parameter is out of range (unsorted
// edges, an unsuppressible bit in suppressMask, more than kBiomeMaxInjections injections, an
// injection of Ocean or Lake or with an empty height range). Otherwise a pure function of
// (inputs, params).
inline std::optional<BiomeMap> generateBiomes(const BiomeInputs& inputs, const BiomeParams& params = {})
{
    if (!detail::biomeInputValid(inputs) || !detail::biomeParamsValid(params)) return std::nullopt;
    const ClimateMap& cl = *inputs.climate;
    const std::size_t cells = cl.temperatureC10.size();
    BiomeMap out;
    out.seed = cl.seed;
    out.width = cl.width;
    out.height = cl.height;
    out.biome.assign(cells, static_cast<std::uint8_t>(BiomeId::Ocean));
    out.secondary.assign(cells, 0);
    out.blend.assign(cells, 0);
    detail::classifyBiomes(inputs, params, out);
    detail::injectBiomes(*inputs.hydrology, params, out);
    detail::suppressBiomes(params, out);
    detail::blendBiomes(out);
    return out;
}

// Order-sensitive digest of every field of the map; the golden value that guards determinism.
inline std::uint64_t hashBiomeMap(const BiomeMap& map)
{
    std::uint64_t h = splitmix64(map.seed ^ detail::kBiomeDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    const auto at = [](const std::vector<std::uint8_t>& layer, std::size_t c) -> std::uint64_t { return c < layer.size() ? layer[c] : 0; };
    for (std::size_t c = 0; c < map.biome.size(); ++c) {
        mix((static_cast<std::uint64_t>(map.biome[c]) << 16) | (at(map.secondary, c) << 8) | at(map.blend, c));
    }
    return h;
}

}
