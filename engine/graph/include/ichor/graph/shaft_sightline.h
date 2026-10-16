#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ichor::graph::shaft {

// A room on one floor as a grid rectangle [x0,x1) x [y0,y1). Rooms are the
// layout's view of mission nodes; `id` is the caller's own key.
struct Room {
    std::uint32_t id = 0;
    std::int32_t floor = 0;
    std::int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// A walkable connection between two rooms by id (corridor, stair, drop). Only
// used to measure how far apart two rooms are on foot.
struct Link {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
};

// A grid cell on a floor that a required path crosses (corridor, stair
// landing, door approach). Shafts never open a hole in one.
struct Cell {
    std::int32_t floor = 0;
    std::int32_t x = 0, y = 0;
};

struct Params {
    std::uint64_t seed = 0;
    std::uint32_t maxShafts = 4;       // clamped to kMaxShafts
    std::uint32_t maxSpan = 2;         // most floors one shaft may cross, clamped to [1, kMaxSpanLimit]
    std::uint32_t minWalkDistance = 3; // rooms closer than this on foot are not worth a sightline
};
inline constexpr std::uint32_t kMaxShafts = 64;
inline constexpr std::uint32_t kMaxSpanLimit = 8;
inline constexpr std::size_t kMaxRooms = 256;
inline constexpr std::int32_t kMaxRoomSide = 256;
inline constexpr std::int32_t kMaxFloor = 1024;
inline constexpr std::uint32_t kUnreachable = 0xFFFFFFFFu;

// One open shaft: a hole at (x, y) in the floor of `upper`, looking down onto
// `lower`. Every floor strictly between is empty at that cell.
struct Shaft {
    std::uint32_t upper = 0;
    std::uint32_t lower = 0;
    std::int32_t x = 0, y = 0;
    std::int32_t upperFloor = 0;
    std::int32_t lowerFloor = 0;
    std::uint32_t walkDistance = 0; // steps on foot between the rooms; kUnreachable if none
};

// Chooses where open shafts connect floors so rooms on different floors get a
// sightline. Pure and deterministic in (rooms, links, reserved, params) and
// independent of input order. Prefers pairs far apart on foot (a room you
// will reach later). Guarantees: at most maxShafts; no shaft on a reserved cell
// of its upper floor; none through a room or reserved cell of an intermediate
// floor; no two shafts share a column cell; each room used at most once.
// Rooms with an empty or oversized rectangle, an out-of-range floor, a
// duplicate id, or beyond kMaxRooms are ignored; empty or single-floor input
// yields no shafts. Never throws.
std::vector<Shaft> chooseShafts(const std::vector<Room>& rooms, const std::vector<Link>& links,
                                const std::vector<Cell>& reserved, const Params& params);

}
