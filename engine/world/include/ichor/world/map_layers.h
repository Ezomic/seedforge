#pragma once

#include "ichor/world/map_image.h"

#include <worldforge/forge_world.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ichor {

// The toggleable layers of the in-game map (doc 08 phase 12), drawn over the shaded terrain picture
// from the forged data: biome colours, roads, faction territories and rivers.
enum class MapLayer : std::uint8_t { Biomes, Roads, Territories, Rivers };

constexpr std::size_t kMapLayerCount = 4;

const char* mapLayerName(MapLayer layer);

// Which layers are on. Nothing is on by default, so a map with no layers looks as it always did.
class MapLayerSet {
public:
    bool on(MapLayer layer) const { return (bits_ & bit(layer)) != 0; }
    void set(MapLayer layer, bool value) { bits_ = static_cast<std::uint8_t>(value ? (bits_ | bit(layer)) : (bits_ & ~bit(layer))); }
    void toggle(MapLayer layer) { bits_ = static_cast<std::uint8_t>(bits_ ^ bit(layer)); }
    bool operator==(const MapLayerSet&) const = default;

private:
    static std::uint8_t bit(MapLayer layer) { return static_cast<std::uint8_t>(1u << static_cast<unsigned>(layer)); }
    std::uint8_t bits_ = 0;
};

// The terrain picture plus one translucent overlay per layer, all the same size. Building it does the
// per-cell work once; composite() is a cheap blend of the layers that are on, so toggling is quick.
class MapLayerStack {
public:
    MapLayerStack() = default;

    // `base` with overlays taken from the forged world's grids. A layer whose phase the world did not
    // reach has no overlay and changes nothing. An empty or non-square `base` gives an empty stack.
    MapLayerStack(MapImage base, const worldforge::ForgedWorld& world);

    bool empty() const { return base_.empty(); }
    const MapImage& base() const { return base_; }

    // True when the layer has at least one visible pixel to draw.
    bool hasLayer(MapLayer layer) const { return present_[static_cast<std::size_t>(layer)]; }

    // The base picture with every layer in `layers` blended over it, in the order of MapLayer. An empty
    // stack gives an empty image.
    MapImage composite(MapLayerSet layers) const;

private:
    MapImage base_;
    std::array<std::vector<std::uint8_t>, kMapLayerCount> overlay_; // RGBA, straight alpha, base size
    std::array<bool, kMapLayerCount> present_{};
};

}
