#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// Doc 05: an enemy's silhouette must survive dense foliage. This works on plain masks, so it
// needs no renderer: the enemy as it would be drawn against a clutter mask (what the scatter
// pass puts on screen), each with a flat luminance in [0, 1]. Only integer arithmetic and plain
// float add, subtract and divide are used, so a result is the same on every platform.

inline constexpr int kSilhouetteMaxDim = 4096;

struct SilhouetteMask {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> cells; // row-major, non-zero means set

    bool valid() const
    {
        return width > 0 && height > 0 && width <= kSilhouetteMaxDim && height <= kSilhouetteMaxDim
            && cells.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
    bool at(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < width && y < height
            && cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] != 0;
    }
    std::size_t count() const
    {
        std::size_t n = 0;
        for (const std::uint8_t c : cells) {
            n += c != 0 ? 1 : 0;
        }
        return n;
    }
};

enum class ClutterDepth : std::uint8_t {
    Behind,  // foliage is the backdrop the enemy is read against
    InFront, // foliage covers the enemy where the masks overlap
};

struct SilhouetteParams {
    float enemyLuma = 0.1f;        // flat tone of the enemy
    float clutterLuma = 0.4f;      // flat tone of the foliage
    float backdropLuma = 0.6f;     // tone of whatever is neither enemy nor foliage
    float minEdgeContrast = 0.15f; // an outline edge reads when its luma gap is at least this
    float minLegibleEdges = 0.6f;  // share of the outline that must read
    float maxOcclusion = 0.5f;     // share of the enemy foliage may cover
};

enum class SilhouetteStatus : std::uint8_t {
    Ok,
    BadMask, // wrong cell count, or a side outside 1..kSilhouetteMaxDim
    SizeMismatch,
    EmptyEnemy,
    BadParams, // NaN, infinite, or outside [0, 1]
};

struct SilhouetteResult {
    SilhouetteStatus status = SilhouetteStatus::Ok;
    float occlusion = 0.0f;    // share of enemy cells covered by foliage
    float meanContrast = 0.0f; // mean luma gap over the visible outline
    float legibleEdges = 0.0f; // share of the whole outline that is visible and contrasts enough
    bool passes = false;
};

inline bool silhouetteUnit(float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; }

inline bool silhouetteParamsValid(const SilhouetteParams& p)
{
    return silhouetteUnit(p.enemyLuma) && silhouetteUnit(p.clutterLuma) && silhouetteUnit(p.backdropLuma)
        && silhouetteUnit(p.minEdgeContrast) && silhouetteUnit(p.minLegibleEdges) && silhouetteUnit(p.maxOcclusion);
}

inline SilhouetteResult checkSilhouette(const SilhouetteMask& enemy, const SilhouetteMask& clutter, ClutterDepth depth,
    const SilhouetteParams& params)
{
    SilhouetteResult out;
    if (!enemy.valid() || !clutter.valid()) {
        out.status = SilhouetteStatus::BadMask;
        return out;
    }
    if (enemy.width != clutter.width || enemy.height != clutter.height) {
        out.status = SilhouetteStatus::SizeMismatch;
        return out;
    }
    if (!silhouetteParamsValid(params)) {
        out.status = SilhouetteStatus::BadParams;
        return out;
    }
    const std::size_t total = enemy.count();
    if (total == 0) {
        out.status = SilhouetteStatus::EmptyEnemy;
        return out;
    }

    const bool front = depth == ClutterDepth::InFront;
    std::size_t covered = 0;
    std::size_t edges = 0;
    std::size_t legible = 0;
    std::size_t visibleEdges = 0;
    float contrastSum = 0.0f;
    constexpr int dx[4] = {1, -1, 0, 0};
    constexpr int dy[4] = {0, 0, 1, -1};

    for (int y = 0; y < enemy.height; ++y) {
        for (int x = 0; x < enemy.width; ++x) {
            if (!enemy.at(x, y)) {
                continue;
            }
            const bool hidden = front && clutter.at(x, y);
            covered += hidden ? 1 : 0;
            for (int d = 0; d < 4; ++d) {
                const int nx = x + dx[d];
                const int ny = y + dy[d];
                if (enemy.at(nx, ny)) {
                    continue;
                }
                ++edges;
                if (hidden) {
                    continue;
                }
                const float bg = clutter.at(nx, ny) ? params.clutterLuma : params.backdropLuma;
                const float gap = params.enemyLuma > bg ? params.enemyLuma - bg : bg - params.enemyLuma;
                ++visibleEdges;
                contrastSum += gap;
                legible += gap >= params.minEdgeContrast ? 1 : 0;
            }
        }
    }

    out.occlusion = static_cast<float>(covered) / static_cast<float>(total);
    out.meanContrast = visibleEdges > 0 ? contrastSum / static_cast<float>(visibleEdges) : 0.0f;
    out.legibleEdges = edges > 0 ? static_cast<float>(legible) / static_cast<float>(edges) : 0.0f;
    out.passes = out.legibleEdges >= params.minLegibleEdges && out.occlusion <= params.maxOcclusion;
    return out;
}

// A deterministic foliage mask: each cell is set with probability density (0..1), from an
// integer hash, so a test or a review can reproduce the same clutter from a seed.
inline SilhouetteMask denseFoliageMask(int width, int height, float density, std::uint32_t seed)
{
    SilhouetteMask m;
    if (width <= 0 || height <= 0 || width > kSilhouetteMaxDim || height > kSilhouetteMaxDim) {
        return m;
    }
    m.width = width;
    m.height = height;
    m.cells.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    if (!(density > 0.0f)) {
        return m;
    }
    const std::uint32_t threshold = density >= 1.0f ? 0xFFFFFFFFu : static_cast<std::uint32_t>(density * 4294967040.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint32_t h = seed ^ (static_cast<std::uint32_t>(x) * 0x9E3779B1u) ^ (static_cast<std::uint32_t>(y) * 0x85EBCA6Bu);
            h ^= h >> 16;
            h *= 0x7FEB352Du;
            h ^= h >> 15;
            h *= 0x846CA68Bu;
            h ^= h >> 16;
            m.cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = h <= threshold ? 1 : 0;
        }
    }
    return m;
}

struct NamedSilhouette {
    std::string id;
    SilhouetteMask mask;
};

struct NamedSilhouetteResult {
    std::string id;
    SilhouetteResult behind;
    SilhouetteResult inFront;
    bool passes() const
    {
        return behind.status == SilhouetteStatus::Ok && inFront.status == SilhouetteStatus::Ok && behind.passes && inFront.passes;
    }
};

// Every shape is tested against foliage as the backdrop and with foliage in front. Order follows the input.
inline std::vector<NamedSilhouetteResult> checkSilhouettes(const std::vector<NamedSilhouette>& shapes, const SilhouetteMask& clutter,
    const SilhouetteParams& params)
{
    std::vector<NamedSilhouetteResult> out;
    out.reserve(shapes.size());
    for (const NamedSilhouette& s : shapes) {
        out.push_back({s.id, checkSilhouette(s.mask, clutter, ClutterDepth::Behind, params),
            checkSilhouette(s.mask, clutter, ClutterDepth::InFront, params)});
    }
    return out;
}

}
