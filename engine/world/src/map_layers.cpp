#include "ichor/world/map_layers.h"

#include <algorithm>
#include <utility>

namespace ichor {

namespace {

struct Colour {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

constexpr std::array<Colour, worldforge::kBiomeCount> kBiomeColours = {{
    {28, 62, 112},   // Ocean
    {58, 110, 170},  // Lake
    {232, 240, 246}, // Glacier
    {168, 180, 160}, // Tundra
    {44, 98, 76},    // Taiga
    {34, 120, 52},   // Forest
    {132, 184, 78},  // Grassland
    {222, 190, 112}, // Desert
    {196, 168, 72},  // Savanna
    {16, 140, 92},   // Rainforest
    {150, 140, 170}, // Alpine
    {104, 100, 96},  // Crag
    {78, 120, 104},  // Marsh
    {200, 206, 96},  // LowlandFarm
    {196, 140, 150}, // SaltBlight
    {110, 90, 130},  // TidalRuin
}};

constexpr std::array<Colour, 8> kFactionColours = {{
    {214, 64, 64},
    {64, 112, 214},
    {222, 190, 48},
    {64, 180, 96},
    {176, 80, 200},
    {240, 140, 40},
    {64, 200, 200},
    {220, 110, 170},
}};

constexpr Colour kFord{250, 250, 250}; // a road on a river; it stays visible when cells share a pixel
constexpr Colour kRiver{40, 120, 255};
// Okabe-Ito, the set chosen to stay apart under every common colour vision deficiency.
constexpr std::array<Colour, 8> kColourblindFactionColours = {{
    {213, 94, 0},
    {0, 114, 178},
    {240, 228, 66},
    {0, 158, 115},
    {204, 121, 167},
    {230, 159, 0},
    {86, 180, 233},
    {153, 153, 153},
}};

constexpr Colour kRoadHalo{244, 226, 170};
constexpr Colour kLake{96, 170, 255}; // lighter than a river so a lake reads as a body of water
constexpr std::uint8_t kBiomeAlpha = 120;
constexpr std::uint8_t kTerritoryAlpha = 100;

using Overlay = std::vector<std::uint8_t>;

void put(Overlay& overlay, std::size_t pixel, Colour colour, std::uint8_t alpha)
{
    std::uint8_t* out = &overlay[pixel * 4];
    out[0] = colour.r;
    out[1] = colour.g;
    out[2] = colour.b;
    out[3] = alpha;
}

bool gridUsable(int width, int height, std::size_t cells)
{
    return width > 0 && height > 0 && cells == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

// The cell under the middle of a pixel.
int cellOf(std::uint32_t pixel, std::uint32_t side, int cells)
{
    const std::uint64_t cell = (static_cast<std::uint64_t>(pixel) * 2 + 1) * static_cast<std::uint64_t>(cells) / (static_cast<std::uint64_t>(side) * 2);
    return static_cast<int>(std::min<std::uint64_t>(cell, static_cast<std::uint64_t>(cells - 1)));
}

// The pixel a cell falls in, so a thin feature (a road, a river) is not lost when the grid is finer than the picture.
std::uint32_t pixelOf(int cell, int cells, std::uint32_t side)
{
    const std::uint64_t pixel = (static_cast<std::uint64_t>(cell) * 2 + 1) * side / (static_cast<std::uint64_t>(cells) * 2);
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(pixel, side - 1));
}

template <typename Colourise>
Overlay sampledOverlay(std::uint32_t side, int width, int height, Colourise colourise)
{
    Overlay overlay(static_cast<std::size_t>(side) * side * 4, 0);
    for (std::uint32_t py = 0; py < side; ++py) {
        const std::size_t row = static_cast<std::size_t>(cellOf(py, side, height)) * static_cast<std::size_t>(width);
        for (std::uint32_t px = 0; px < side; ++px) {
            colourise(overlay, static_cast<std::size_t>(py) * side + px, row + static_cast<std::size_t>(cellOf(px, side, width)));
        }
    }
    return overlay;
}

template <typename Colourise>
Overlay splattedOverlay(std::uint32_t side, int width, int height, Colourise colourise)
{
    Overlay overlay(static_cast<std::size_t>(side) * side * 4, 0);
    for (int cy = 0; cy < height; ++cy) {
        const std::size_t row = static_cast<std::size_t>(pixelOf(cy, height, side)) * side;
        for (int cx = 0; cx < width; ++cx) {
            colourise(overlay, row + pixelOf(cx, width, side), static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx));
        }
    }
    return overlay;
}

// A pale edge around every road pixel, so a one-pixel road still stands out from the biome colours.
void haloRoads(Overlay& overlay, std::uint32_t side)
{
    const Overlay roads = overlay;
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y) * side + x;
            if (roads[pixel * 4 + 3] == 0) {
                continue;
            }
            const std::int32_t around[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (const auto& step : around) {
                const std::int64_t nx = static_cast<std::int64_t>(x) + step[0];
                const std::int64_t ny = static_cast<std::int64_t>(y) + step[1];
                if (nx < 0 || ny < 0 || nx >= side || ny >= side) {
                    continue;
                }
                const std::size_t neighbour = static_cast<std::size_t>(ny) * side + static_cast<std::size_t>(nx);
                if (roads[neighbour * 4 + 3] == 0) {
                    put(overlay, neighbour, kRoadHalo, 210);
                }
            }
        }
    }
}

bool anyVisible(const Overlay& overlay)
{
    for (std::size_t i = 3; i < overlay.size(); i += 4) {
        if (overlay[i] != 0) {
            return true;
        }
    }
    return false;
}

std::size_t slot(MapLayer layer)
{
    return static_cast<std::size_t>(layer);
}

}

const char* mapLayerName(MapLayer layer)
{
    switch (layer) {
    case MapLayer::Biomes: return "Biomes";
    case MapLayer::Roads: return "Roads";
    case MapLayer::Territories: return "Territories";
    case MapLayer::Rivers: return "Rivers";
    }
    return "";
}

MapLayerStack::MapLayerStack(MapImage base, const worldforge::ForgedWorld& world)
    : base_(std::move(base))
{
    if (base_.empty() || base_.width != base_.height) {
        base_ = MapImage{};
        return;
    }
    const std::uint32_t side = base_.width;
    const auto& biomes = world.biomes();
    if (biomes && gridUsable(biomes->width, biomes->height, biomes->biome.size())) {
        overlay_[slot(MapLayer::Biomes)] = sampledOverlay(side, biomes->width, biomes->height, [&](Overlay& o, std::size_t pixel, std::size_t cell) {
            put(o, pixel, kBiomeColours[biomes->biome[cell] % worldforge::kBiomeCount], kBiomeAlpha);
        });
    }
    const auto& roads = world.roads();
    if (roads && gridUsable(roads->width, roads->height, roads->road.size())) {
        overlay_[slot(MapLayer::Roads)] = splattedOverlay(side, roads->width, roads->height, [&](Overlay& o, std::size_t pixel, std::size_t cell) {
            const bool ford = roads->road[cell] == 2;
            const bool fordHere = o[pixel * 4] == kFord.r && o[pixel * 4 + 3] != 0;
            if (roads->road[cell] != 0 && !fordHere) {
                put(o, pixel, ford ? kFord : Colour{92, 52, 20}, 255);
            }
        });
        haloRoads(overlay_[slot(MapLayer::Roads)], side);
    }
    const auto& politics = world.politics();
    if (politics && gridUsable(politics->width, politics->height, politics->owner.size())) {
        const auto territories = [&](const std::array<Colour, 8>& colours) {
            return sampledOverlay(side, politics->width, politics->height, [&](Overlay& o, std::size_t pixel, std::size_t cell) {
                const std::uint8_t owner = politics->owner[cell];
                if (owner != worldforge::kPoliticsNoOwner) {
                    put(o, pixel, colours[owner % colours.size()], kTerritoryAlpha);
                }
            });
        };
        overlay_[slot(MapLayer::Territories)] = territories(kFactionColours);
        colourblindTerritories_ = territories(kColourblindFactionColours);
    }
    const auto& hydrology = world.hydrology();
    if (hydrology && gridUsable(hydrology->width, hydrology->height, hydrology->river.size())) {
        Overlay waters = sampledOverlay(side, hydrology->width, hydrology->height, [&](Overlay& o, std::size_t pixel, std::size_t cell) {
            if (cell < hydrology->lakeDepthM.size() && hydrology->lakeDepthM[cell] != 0) {
                put(o, pixel, kLake, 255);
            }
        });
        for (int cy = 0; cy < hydrology->height; ++cy) {
            const std::size_t row = static_cast<std::size_t>(pixelOf(cy, hydrology->height, side)) * side;
            for (int cx = 0; cx < hydrology->width; ++cx) {
                const std::size_t cell = static_cast<std::size_t>(cy) * static_cast<std::size_t>(hydrology->width) + static_cast<std::size_t>(cx);
                if (hydrology->river[cell] != 0) {
                    put(waters, row + pixelOf(cx, hydrology->width, side), kRiver, 255);
                }
            }
        }
        overlay_[slot(MapLayer::Rivers)] = std::move(waters);
    }
    for (std::size_t i = 0; i < kMapLayerCount; ++i) {
        present_[i] = anyVisible(overlay_[i]);
    }
}

MapImage MapLayerStack::composite(MapLayerSet layers, bool colourblindSafe) const
{
    if (base_.empty()) {
        return MapImage{};
    }
    MapImage out = base_;
    for (std::size_t layer = 0; layer < kMapLayerCount; ++layer) {
        if (!layers.on(static_cast<MapLayer>(layer)) || !present_[layer]) {
            continue;
        }
        const bool swapped = colourblindSafe && layer == slot(MapLayer::Territories);
        const Overlay& overlay = swapped ? colourblindTerritories_ : overlay_[layer];
        for (std::size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
            const unsigned alpha = overlay[i + 3];
            if (alpha == 0) {
                continue;
            }
            for (std::size_t c = 0; c < 3; ++c) {
                out.rgba[i + c] = static_cast<std::uint8_t>((overlay[i + c] * alpha + out.rgba[i + c] * (255 - alpha) + 127) / 255);
            }
        }
    }
    return out;
}

}
