#pragma once

#include "ichor/world/map_image.h"
#include "ichor/world/placed_settlements.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// How far the player sees around them on the map, as a fraction of the map's width.
constexpr float kMapRevealRadius = 0.045f;

// Which parts of the map the player has seen (fog of war), as a square grid of cells the size of the map
// picture. A cell is seen once the player has come within the reveal radius of it; it never goes dark
// again. Integer math only, so the same walk gives the same grid on every machine. Presentation: nothing
// here feeds the simulation or any hash.
class MapDiscovery {
public:
    explicit MapDiscovery(std::uint32_t side = kMapSide);

    std::uint32_t side() const { return side_; }

    // Marks every cell whose centre lies within `radius` (a fraction of the map's width) of `at`. A radius
    // that is not a number or is not positive reveals the one cell under `at`; one past the whole map is
    // clamped to it. `at` outside the map is clamped to the edge, a position that is not a number is ignored.
    void reveal(MapPoint at, float radius);

    // reveal() at the start, the end and every step between them, so a long stride leaves no gaps.
    void revealSegment(MapPoint from, MapPoint to, float radius);

    bool seen(MapPoint at) const;
    bool seenCell(std::uint32_t x, std::uint32_t y) const;
    std::size_t seenCount() const { return seenCount_; }

    bool operator==(const MapDiscovery&) const = default;

private:
    std::uint32_t side_ = kMapSide;
    std::vector<std::uint8_t> seen_;
    std::size_t seenCount_ = 0;
};

// `image` with every unseen pixel replaced by the fog colour. An image that is not the grid's size is
// returned unchanged.
MapImage withFog(const MapImage& image, const MapDiscovery& discovery);

// A settlement as the map draws it: where it lies, its size tier (0 hamlet to 3 city) and its name.
struct MapSettlement {
    std::string id;
    std::string name;
    std::uint8_t tier = 0;
    MapPoint at;
};

// A made-up name for a settlement, the same for the same world seed and settlement id.
std::string settlementMapName(std::uint64_t worldSeed, std::string_view settlementId);

// The sites as map settlements, in the sites' order, with names that differ from one another.
std::vector<MapSettlement> mapSettlements(const WorldDesc& desc, std::uint64_t worldSeed, const std::vector<SettlementSite>& sites);

}
