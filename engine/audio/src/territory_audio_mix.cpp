#include "ichor/audio/territory_audio_mix.h"

namespace ichor {

namespace {

bool finite(float v)
{
    return v - v == 0.0f;
}

float clamp01(float v)
{
    if (!(v > 0.0f)) {
        return 0.0f;
    }
    return v > 1.0f ? 1.0f : v;
}

bool gridUsable(const TerritoryAudioGrid& g)
{
    if (g.width == 0 || g.height == 0) {
        return false;
    }
    if (static_cast<std::size_t>(g.width) > kTerritoryAudioMaxCells / g.height) {
        return false;
    }
    return finite(g.cellSize) && g.cellSize > 0.0f && finite(g.originX) && finite(g.originZ);
}

struct Axis {
    std::uint32_t i0 = 0;
    std::uint32_t i1 = 0;
    float frac = 0.0f;
};

// Cell-centre coordinates of p, clamped to the grid, so the edge value is held off the map.
Axis axisOf(float p, float origin, float cellSize, std::uint32_t count)
{
    float t = (p - origin) / cellSize - 0.5f;
    const float top = static_cast<float>(count - 1);
    if (!finite(t) || t < 0.0f) {
        t = 0.0f;
    } else if (t > top) {
        t = top;
    }
    Axis a;
    a.i0 = static_cast<std::uint32_t>(t);
    a.i1 = a.i0 + 1 < count ? a.i0 + 1 : a.i0;
    a.frac = t - static_cast<float>(a.i0);
    return a;
}

float strengthAt(const std::vector<std::uint16_t>& s, std::uint32_t width, std::uint32_t x, std::uint32_t y)
{
    std::uint32_t v = s[static_cast<std::size_t>(y) * width + x];
    if (v > kTerritoryAudioMaxStrength) {
        v = kTerritoryAudioMaxStrength;
    }
    return static_cast<float>(v) / static_cast<float>(kTerritoryAudioMaxStrength);
}

}  // namespace

TerritoryAudioMix territoryAudioMix(const TerritoryAudioGrid& grid, const std::vector<TerritoryAudioLayer>& layers, float x, float z)
{
    TerritoryAudioMix mix;
    mix.gains.assign(layers.size(), 0.0f);
    if (!gridUsable(grid) || !finite(x) || !finite(z)) {
        return mix;
    }

    const std::size_t factions = layers.size() < grid.strength.size() ? layers.size() : grid.strength.size();
    const std::size_t cells = static_cast<std::size_t>(grid.width) * grid.height;
    const Axis ax = axisOf(x, grid.originX, grid.cellSize, grid.width);
    const Axis az = axisOf(z, grid.originZ, grid.cellSize, grid.height);

    std::vector<float> weights(layers.size(), 0.0f);
    float sum = 0.0f;
    for (std::size_t f = 0; f < factions && f < kTerritoryAudioMaxFactions; ++f) {
        const std::vector<std::uint16_t>& s = grid.strength[f];
        if (s.size() != cells) {
            continue;
        }
        const float top = strengthAt(s, grid.width, ax.i0, az.i0) * (1.0f - ax.frac) + strengthAt(s, grid.width, ax.i1, az.i0) * ax.frac;
        const float bottom = strengthAt(s, grid.width, ax.i0, az.i1) * (1.0f - ax.frac) + strengthAt(s, grid.width, ax.i1, az.i1) * ax.frac;
        weights[f] = top * (1.0f - az.frac) + bottom * az.frac;
        sum += weights[f];
    }

    const float norm = sum > 1.0f ? sum : 1.0f;
    float best = 0.0f;
    for (std::size_t f = 0; f < layers.size(); ++f) {
        if (layers[f].layerId.empty()) {
            continue;
        }
        const float g = clamp01(weights[f] / norm) * clamp01(layers[f].maxGain);
        mix.gains[f] = g;
        if (g > best) {
            best = g;
            mix.dominant = static_cast<int>(f);
        }
    }
    return mix;
}

std::vector<float> territoryAudioStep(const std::vector<float>& current, const std::vector<float>& target, float dt, float fadeSeconds)
{
    std::vector<float> out(target.size(), 0.0f);
    const bool snap = !(fadeSeconds > 0.0f) || !finite(fadeSeconds);
    const bool stepOk = finite(dt) && dt > 0.0f;
    const float maxStep = snap ? 1.0f : (stepOk ? dt / fadeSeconds : 0.0f);
    for (std::size_t i = 0; i < target.size(); ++i) {
        const float want = clamp01(target[i]);
        const float have = i < current.size() ? clamp01(current[i]) : 0.0f;
        if (snap) {
            out[i] = want;
        } else if (want > have) {
            out[i] = want - have > maxStep ? have + maxStep : want;
        } else {
            out[i] = have - want > maxStep ? have - maxStep : want;
        }
    }
    return out;
}

}  // namespace ichor
