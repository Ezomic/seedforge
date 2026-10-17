#pragma once

#include <worldforge/subseed.h>
#include <worldforge/tectonics_map.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <vector>

// Phase 10 validation, landmark visibility coverage (doc 11, "Navigation: landmarks over
// waypoints"): sample points across the walkable world and require that from each one at least
// one distinctive landmark silhouette is visible or within a short walk. A seed whose coverage
// falls below the threshold fails. The height field and the landmark list are inputs; POI
// placement does not exist yet, so nothing here decides where landmarks go. Nothing is mutated.
//
// Inputs (LandmarkCoverageInputs): a row-major height field (index = y * width + x, metres, within
// the tectonics height range, size kTectonicsMinSize..kTectonicsMaxSize per side) and a plain list
// of at most kLandmarkMaxLandmarks landmarks. A landmark is a cell plus a silhouette height in
// metres above the ground at that cell; its visible top is ground + heightM.
//
// Sampling: a walkable cell is one whose height is above params.seaLevelM. Samples sit on a
// regular grid with a stride chosen as the smallest value that keeps the grid at or below
// params.maxSamples points; only walkable grid points count.
//
// A sample is covered when some landmark is
//  - near:    within nearRangeCells (Euclidean, straight line, terrain ignored), or
//  - visible: within viewRangeCells and an integer line of sight reaches the landmark top. The ray
//             runs from the eye (ground + eyeHeightM) to the landmark top, one step per cell along
//             the major axis; the landmark is hidden when any intermediate cell's terrain is
//             higher than the ray at that cell. No earth curvature, no atmosphere.
//
// Output (LandmarkCoverageReport): sample counts, coverage in permille (covered * 1000 / samples,
// rounded down), pass/fail against minCoveragePermille, and the first kLandmarkMaxGaps uncovered
// samples in scan order. With zero walkable samples there is nothing to validate and the report
// fails (coverage 0): an all-ocean world is not a navigable one.
//
// Determinism: integer only, no libm, no floating point, no unordered container. Memory is O(1)
// beyond the report: the height field is read in place and the report holds at most
// kLandmarkMaxGaps samples. Work is at most maxSamples * landmarks * viewRangeCells
// line-of-sight steps (worst case 8192 * 128 * 255, about 270 million; typical runs exit at the
// first visible landmark and use the default 4096 samples).
namespace worldforge {

constexpr int kLandmarkMaxLandmarks = 128;
constexpr int kLandmarkMaxHeightM = 2000;
constexpr int kLandmarkMaxRangeCells = 255;
constexpr int kLandmarkMaxSamples = 8192;
constexpr int kLandmarkMaxEyeM = 10;
constexpr std::size_t kLandmarkMaxGaps = 16;

struct Landmark {
    int x = 0;
    int y = 0;
    int heightM = 0; // silhouette height above the ground at (x, y), in [1, kLandmarkMaxHeightM]
};

struct LandmarkCoverageParams {
    int seaLevelM = 0;             // cells at or below this are not walkable, in the tectonics height range
    int eyeHeightM = 2;            // observer eye above ground, in [0, kLandmarkMaxEyeM]
    int viewRangeCells = 96;       // farthest a landmark can be seen from, in [1, 255]
    int nearRangeCells = 12;       // landmark within this counts without line of sight, in [0, 255]
    int maxSamples = 4096;         // sample grid size cap, in [1, kLandmarkMaxSamples]
    int minCoveragePermille = 900; // pass threshold, in [0, 1000]
};

struct LandmarkCoverageInputs {
    int width = 0;
    int height = 0;
    std::span<const std::int16_t> heightM;
    std::span<const Landmark> landmarks;
};

struct CoverageSample {
    int x = 0;
    int y = 0;
};

struct LandmarkCoverageReport {
    int stride = 0;
    int sampleCount = 0;
    int visibleCount = 0; // covered by line of sight (and not by the near rule)
    int nearCount = 0;    // covered by the near rule
    int coveredCount = 0; // visibleCount + nearCount
    int coveragePermille = 0;
    bool passed = false;
    std::vector<CoverageSample> gaps; // first kLandmarkMaxGaps uncovered samples, scan order
};

namespace detail {

constexpr std::uint64_t kLandmarkDomain = 0x6c6e646d726b3130ull;

constexpr std::int64_t landmarkFloorDiv(std::int64_t num, std::int64_t den)
{
    std::int64_t q = num / den;
    if (num % den < 0) --q;
    return q;
}

inline bool landmarkInputValid(const LandmarkCoverageInputs& in)
{
    if (in.width < kTectonicsMinSize || in.width > kTectonicsMaxSize || in.height < kTectonicsMinSize || in.height > kTectonicsMaxSize) return false;
    if (in.heightM.size() != static_cast<std::size_t>(in.width) * static_cast<std::size_t>(in.height)) return false;
    if (in.landmarks.size() > static_cast<std::size_t>(kLandmarkMaxLandmarks)) return false;
    const bool heightsOk = std::all_of(in.heightM.begin(), in.heightM.end(), [](std::int16_t v) { return v >= kTectonicsMinHeightM && v <= kTectonicsMaxHeightM; });
    if (!heightsOk) return false;
    return std::all_of(in.landmarks.begin(), in.landmarks.end(), [&](const Landmark& l) {
        return l.x >= 0 && l.x < in.width && l.y >= 0 && l.y < in.height && l.heightM >= 1 && l.heightM <= kLandmarkMaxHeightM;
    });
}

inline bool landmarkParamsValid(const LandmarkCoverageParams& p)
{
    return p.seaLevelM >= kTectonicsMinHeightM && p.seaLevelM <= kTectonicsMaxHeightM && p.eyeHeightM >= 0 && p.eyeHeightM <= kLandmarkMaxEyeM
        && p.viewRangeCells >= 1 && p.viewRangeCells <= kLandmarkMaxRangeCells && p.nearRangeCells >= 0 && p.nearRangeCells <= kLandmarkMaxRangeCells
        && p.maxSamples >= 1 && p.maxSamples <= kLandmarkMaxSamples && p.minCoveragePermille >= 0 && p.minCoveragePermille <= 1000;
}

inline int landmarkStride(int width, int height, int maxSamples)
{
    int s = 1;
    while (static_cast<std::int64_t>((width + s - 1) / s) * ((height + s - 1) / s) > maxSamples) ++s;
    return s;
}

// True when the ray from (sx, sy) at eyeM to the top of `l` clears all terrain strictly between.
inline bool landmarkLineOfSight(const LandmarkCoverageInputs& in, int sx, int sy, std::int64_t eyeM, const Landmark& l)
{
    const int dx = l.x - sx;
    const int dy = l.y - sy;
    const int n = std::max(std::abs(dx), std::abs(dy));
    if (n <= 1) return true;
    const std::int64_t topM = static_cast<std::int64_t>(in.heightM[static_cast<std::size_t>(l.y) * static_cast<std::size_t>(in.width) + static_cast<std::size_t>(l.x)]) + l.heightM;
    const std::int64_t rise = topM - eyeM;
    for (int i = 1; i < n; ++i) {
        const int x = sx + static_cast<int>(landmarkFloorDiv(2LL * dx * i + n, 2LL * n));
        const int y = sy + static_cast<int>(landmarkFloorDiv(2LL * dy * i + n, 2LL * n));
        const std::int64_t rayM = eyeM + landmarkFloorDiv(rise * i, n);
        if (in.heightM[static_cast<std::size_t>(y) * static_cast<std::size_t>(in.width) + static_cast<std::size_t>(x)] > rayM) return false;
    }
    return true;
}

}

// Returns nullopt when the input is malformed (size outside the kTectonics* limits, a height field
// of the wrong length or with a height outside the allowed range, too many landmarks, a landmark
// off the map or with a height outside [1, kLandmarkMaxHeightM]) or a parameter is out of range.
// Otherwise a pure function of (inputs, params).
inline std::optional<LandmarkCoverageReport> validateLandmarkCoverage(const LandmarkCoverageInputs& in, const LandmarkCoverageParams& params = {})
{
    if (!detail::landmarkInputValid(in) || !detail::landmarkParamsValid(params)) return std::nullopt;

    LandmarkCoverageReport out;
    out.stride = detail::landmarkStride(in.width, in.height, params.maxSamples);
    const int s = out.stride;
    const std::int64_t viewSq = static_cast<std::int64_t>(params.viewRangeCells) * params.viewRangeCells;
    const std::int64_t nearSq = static_cast<std::int64_t>(params.nearRangeCells) * params.nearRangeCells;

    for (int y = s / 2; y < in.height; y += s) {
        for (int x = s / 2; x < in.width; x += s) {
            const std::int64_t ground = in.heightM[static_cast<std::size_t>(y) * static_cast<std::size_t>(in.width) + static_cast<std::size_t>(x)];
            if (ground <= params.seaLevelM) continue;
            ++out.sampleCount;

            const auto distSq = [&](const Landmark& l) {
                const std::int64_t dx = l.x - x;
                const std::int64_t dy = l.y - y;
                return dx * dx + dy * dy;
            };
            const bool isNear = std::any_of(in.landmarks.begin(), in.landmarks.end(), [&](const Landmark& l) { return distSq(l) <= nearSq; });
            const bool isVisible = !isNear
                && std::any_of(in.landmarks.begin(), in.landmarks.end(), [&](const Landmark& l) {
                       return distSq(l) <= viewSq && detail::landmarkLineOfSight(in, x, y, ground + params.eyeHeightM, l);
                   });
            if (isNear) {
                ++out.nearCount;
            } else if (isVisible) {
                ++out.visibleCount;
            } else if (out.gaps.size() < kLandmarkMaxGaps) {
                out.gaps.push_back({ x, y });
            }
        }
    }

    out.coveredCount = out.nearCount + out.visibleCount;
    out.coveragePermille = out.sampleCount > 0 ? static_cast<int>(static_cast<std::int64_t>(out.coveredCount) * 1000 / out.sampleCount) : 0;
    out.passed = out.sampleCount > 0 && out.coveragePermille >= params.minCoveragePermille;
    return out;
}

inline std::uint64_t hashLandmarkCoverage(const LandmarkCoverageReport& r)
{
    std::uint64_t h = splitmix64(detail::kLandmarkDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(r.stride));
    mix(static_cast<std::uint64_t>(r.sampleCount));
    mix(static_cast<std::uint64_t>(r.visibleCount));
    mix(static_cast<std::uint64_t>(r.nearCount));
    mix(static_cast<std::uint64_t>(r.coveragePermille));
    mix(r.passed ? 1 : 0);
    mix(r.gaps.size());
    for (const CoverageSample& g : r.gaps) mix((static_cast<std::uint64_t>(g.x) << 20) | static_cast<std::uint64_t>(g.y));
    return h;
}

}
