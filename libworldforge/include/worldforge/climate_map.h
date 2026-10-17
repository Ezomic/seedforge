#pragma once

#include <worldforge/erosion_map.h>
#include <worldforge/subseed.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Phase 4, Climate (doc 08): a temperature field and a moisture field derived from an ErodedMap.
// Phases 5 and later consume ClimateMap; none of them may mutate it. The input is only read, so
// the grid (width, height, row-major, index = y * width + x) is the ErodedMap's.
//
// Output contract:
//  - ClimateMap::temperatureC10 is the mean annual air temperature in tenths of a degree Celsius
//    (215 = 21.5 C), at the cell's elevation.
//  - ClimateMap::moisture is in [0, kClimateMaxMoisture]. On land it is the precipitation index
//    of the cell: rain dropped from the passing air mass, so windward slopes read high and lee
//    slopes (rain shadows) read low. On ocean cells (height <= 0) it is the humidity the air holds
//    after picking up evaporation, which is what biome and settlement phases use for coastal
//    influence. Moisture is a relative index, not millimetres.
//
// Algorithm:
//  - Temperature: sea-level temperature interpolates linearly from the equator (middle row) to the
//    pole (top and bottom rows) by latitude, then land cells lose lapseC10PerKm per 1000 m of
//    elevation. Ocean cells get no lapse.
//  - Moisture: one prevailing wind direction (one of 8 compass steps). An air mass carries a
//    humidity cell by cell from upwind to downwind. Over ocean it gains evaporation, capped by the
//    saturation capacity of the local temperature. Over land it drops rain three ways: orographic
//    lift (rising terrain squeezes out a share of the humidity), cooling (humidity above the
//    capacity of a colder, higher cell condenses) and a small continental loss on every land cell.
//    A coast cell, whose upwind neighbour is sea or off the map, is not a rise and gets no lift.
//    Ocean humidity beyond the saturation capacity is discarded. Humidity is never regained on the lee side,
//    which produces the rain shadow.
//
// Determinism: integer arithmetic only, no libm, no floating point, no unordered container and no
// randomness, so the result is bit-identical on every platform. Memory is O(cells), about 8 bytes
// per cell of working state, with cells <= kTectonicsMaxSize^2.
namespace worldforge {

constexpr int kClimateMaxMoisture = 1024;
constexpr int kClimateMinTempC10 = -600;
constexpr int kClimateMaxTempC10 = 600;
constexpr int kClimateMaxLapseC10PerKm = 200;
constexpr int kClimateMaxRate = 256;
constexpr int kClimateWindDirections = 8;

struct ClimateParams {
    int equatorTempC10 = 300;  // sea-level temperature at the equator row, in [-600, 600]
    int poleTempC10 = -300;    // sea-level temperature at the top and bottom rows, in [-600, 600]
    int lapseC10PerKm = 65;    // temperature drop per 1000 m above sea level, in [0, 200]
    int windDirection = 0;     // direction the wind blows toward, in [0, 7]: 0 = +x, 1 = +x+y, 2 = +y, ... clockwise
    int evaporation = 96;      // humidity gained per ocean cell, in [0, 1024]
    int inflow = 256;          // humidity entering at the upwind map edge, in [0, 1024]
    int orographicGain = 128;  // lift squeeze (share of 256 per 1024 m of rise), in [0, 256]
    int landLoss = 8;          // share (of 256) of humidity rained out per land cell, in [0, 256]
};

struct ClimateMap {
    std::uint64_t seed = 0; // ErodedMap::seed it was derived from
    int width = 0;
    int height = 0;
    std::vector<std::int16_t> temperatureC10; // tenths of a degree C, size width * height
    std::vector<std::uint16_t> moisture;      // [0, kClimateMaxMoisture], size width * height

    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); }
};

namespace detail {

constexpr std::uint64_t kClimateDomain = 0x636c696d61746534ull;
constexpr int kClimateCapMin = 128;
constexpr int kClimateCapTempLo = -300;
constexpr int kClimateCapTempHi = 400;

inline std::int64_t climateCapacity(std::int64_t tempC10)
{
    const std::int64_t t = std::clamp<std::int64_t>(tempC10, kClimateCapTempLo, kClimateCapTempHi);
    return kClimateCapMin + (t - kClimateCapTempLo) * (kClimateMaxMoisture - kClimateCapMin) / (kClimateCapTempHi - kClimateCapTempLo);
}

inline bool climateInputValid(const ErodedMap& map)
{
    if (map.width < kTectonicsMinSize || map.width > kTectonicsMaxSize || map.height < kTectonicsMinSize || map.height > kTectonicsMaxSize) return false;
    if (map.heightM.size() != static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height)) return false;
    return std::all_of(map.heightM.begin(), map.heightM.end(), [](std::int16_t v) { return v >= kTectonicsMinHeightM && v <= kTectonicsMaxHeightM; });
}

inline bool climateParamsValid(const ClimateParams& p)
{
    const auto inRange = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
    return inRange(p.equatorTempC10, kClimateMinTempC10, kClimateMaxTempC10) && inRange(p.poleTempC10, kClimateMinTempC10, kClimateMaxTempC10)
        && inRange(p.lapseC10PerKm, 0, kClimateMaxLapseC10PerKm) && inRange(p.windDirection, 0, kClimateWindDirections - 1)
        && inRange(p.evaporation, 0, kClimateMaxMoisture) && inRange(p.inflow, 0, kClimateMaxMoisture)
        && inRange(p.orographicGain, 0, kClimateMaxRate) && inRange(p.landLoss, 0, kClimateMaxRate);
}

}

// Returns nullopt when the map is malformed (size outside the kTectonics* limits, a height field
// of the wrong length or with a height outside the allowed range) or a parameter is out of range.
// Otherwise a pure function of (map.seed, map.width, map.height, map.heightM, params).
inline std::optional<ClimateMap> generateClimate(const ErodedMap& map, const ClimateParams& params = {})
{
    if (!detail::climateInputValid(map) || !detail::climateParamsValid(params)) return std::nullopt;

    const int w = map.width;
    const int h = map.height;
    const std::size_t cells = map.heightM.size();
    const auto at = [w](int x, int y) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x); };

    ClimateMap out;
    out.seed = map.seed;
    out.width = w;
    out.height = h;
    out.temperatureC10.resize(cells);
    out.moisture.resize(cells);

    for (int y = 0; y < h; ++y) {
        const std::int64_t offset = static_cast<std::int64_t>(2) * y + 1 - h;
        const std::int64_t latitude = (offset < 0 ? -offset : offset) * 1024 / h;
        const std::int64_t seaLevel = params.equatorTempC10 + (static_cast<std::int64_t>(params.poleTempC10) - params.equatorTempC10) * latitude / 1024;
        for (int x = 0; x < w; ++x) {
            const std::size_t c = at(x, y);
            const std::int64_t elevation = std::max<std::int64_t>(map.heightM[c], 0);
            out.temperatureC10[c] = static_cast<std::int16_t>(seaLevel - elevation * params.lapseC10PerKm / 1000);
        }
    }

    const int dx = detail::kErosionDx[params.windDirection];
    const int dy = detail::kErosionDy[params.windDirection];
    std::vector<std::int32_t> humidity(cells, 0);

    const int yBegin = dy < 0 ? h - 1 : 0;
    const int yStep = dy < 0 ? -1 : 1;
    const int xBegin = dx < 0 ? w - 1 : 0;
    const int xStep = dx < 0 ? -1 : 1;
    for (int yi = 0, y = yBegin; yi < h; ++yi, y += yStep) {
        for (int xi = 0, x = xBegin; xi < w; ++xi, x += xStep) {
            const std::size_t c = at(x, y);
            const int ux = x - dx;
            const int uy = y - dy;
            const bool hasUpwind = ux >= 0 && uy >= 0 && ux < w && uy < h;
            std::int64_t carried = hasUpwind ? humidity[at(ux, uy)] : params.inflow;
            const std::int64_t capacity = detail::climateCapacity(out.temperatureC10[c]);
            const std::int64_t elevation = map.heightM[c];

            if (elevation <= 0) {
                carried = std::min(capacity, carried + params.evaporation);
                out.moisture[c] = static_cast<std::uint16_t>(carried);
            } else {
                const bool upwindIsLand = hasUpwind && map.heightM[at(ux, uy)] > 0;
                const std::int64_t upwindElevation = upwindIsLand ? map.heightM[at(ux, uy)] : elevation;
                const std::int64_t rise = std::max<std::int64_t>(elevation - upwindElevation, 0);
                const std::int64_t lift = std::min<std::int64_t>(rise * params.orographicGain >> 10, kClimateMaxRate);
                std::int64_t rain = carried * lift >> 8;
                carried -= rain;
                if (carried > capacity) {
                    rain += carried - capacity;
                    carried = capacity;
                }
                const std::int64_t loss = carried * params.landLoss >> 8;
                carried -= loss;
                rain += loss;
                out.moisture[c] = static_cast<std::uint16_t>(rain);
            }
            humidity[c] = static_cast<std::int32_t>(carried);
        }
    }
    return out;
}

// Order-sensitive digest of every field of the map; the golden value that guards determinism.
inline std::uint64_t hashClimateMap(const ClimateMap& map)
{
    std::uint64_t h = splitmix64(map.seed ^ detail::kClimateDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    for (const std::int16_t v : map.temperatureC10) mix(static_cast<std::uint64_t>(static_cast<std::uint16_t>(v)));
    for (const std::uint16_t v : map.moisture) mix(static_cast<std::uint64_t>(v));
    return h;
}

}
