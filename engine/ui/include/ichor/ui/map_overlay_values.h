#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "ichor/world/temenos_field.h"

namespace ichor {

// Docs 11 and 13: the map is a diegetic object, and unexplored land is blank paper. An overlay
// (region, territory, temenos) may therefore only ever say something about cells the player has
// revealed. Pure data in, pure data out: nothing here draws, and nothing here reads the world.

enum class MapOverlayKind : std::uint8_t {
    Region,
    Territory,
    Temenos,
};

struct MapOverlayCell {
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator==(const MapOverlayCell&) const = default;
};

constexpr std::uint32_t kMapOverlayNone = 0xFFFFFFFFu;
// Temenos strength is the field value scaled to 0..kMapOverlayTemenosMax.
constexpr std::uint32_t kMapOverlayTemenosMax = 1000;
constexpr std::size_t kMaxMapOverlayCells = 1u << 18;
constexpr float kMapOverlayMaxCellSize = 1.0e6f;

// Where each overlay's values come from. Only the callback of the requested kind is needed.
struct MapOverlaySources {
    // Region id of a cell, or kMapOverlayNone.
    std::function<std::uint32_t(std::int32_t x, std::int32_t y)> regionAt;
    // Index of the faction that holds a cell (contested cells already resolved), or kMapOverlayNone.
    // An index at or beyond factionCount is treated as unowned.
    std::function<std::uint32_t(std::int32_t x, std::int32_t y)> ownerAt;
    std::uint32_t factionCount = 0;
    // Temenos: sanctuaries are sampled at the centre of each cell, (x + 0.5) * cellSize metres.
    std::span<const TemenosSanctuary> sanctuaries;
    float cellSize = 1.0f;
};

enum class MapOverlayStatus : std::uint8_t {
    Ok,
    InvalidInput, // a missing callback for the kind, an unknown kind, or a bad cell size
    Truncated,    // more than kMaxMapOverlayCells distinct revealed cells; the first ones are kept
};

struct MapOverlayEntry {
    MapOverlayCell cell;
    std::uint32_t value = 0; // region id, faction index, or temenos strength (never zero)
};

struct MapOverlay {
    MapOverlayKind kind = MapOverlayKind::Region;
    MapOverlayStatus status = MapOverlayStatus::Ok;
    // Sorted by row then column, unique, and a subset of the revealed cells. Revealed cells the
    // overlay has nothing to say about are left out.
    std::vector<MapOverlayEntry> entries;
};

// Pure and deterministic: the result does not depend on the order or duplicates of `revealed`.
// Cells outside `revealed` are never passed to a callback, so the fog cannot leak through an
// overlay. The callbacks are called at most kMaxMapOverlayCells times.
MapOverlay computeMapOverlay(MapOverlayKind kind, std::span<const MapOverlayCell> revealed, const MapOverlaySources& sources);

}
