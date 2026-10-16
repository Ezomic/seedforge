#pragma once

#include "ichor/traversal/fast_travel.h"

#include <cstdint>

namespace ichor {

// Doc 11, "Fast travel": passage costs coin and world-clock time. The rates are inputs; where they come from
// (data, region economy, faction control) is not decided here. All arithmetic is integer and saturating.
struct TravelCostRates {
    std::uint64_t coinPerLength = 0;
    std::uint64_t coinPerHop = 0;     // flat fare for each road link used
    std::uint64_t ticksPerLength = 0; // world-clock ticks, in the unit of the world clock
    std::uint64_t ticksPerHop = 0;    // flat delay for each road link used (waiting for the caravan)
};

enum class TravelCostRefusal {
    None,
    NotAllowed,       // the plan was refused, so there is nothing to price
    MalformedPlan,    // an allowed plan whose path is shorter than two settlements
    InsufficientCoin, // the cost is known but the purse does not cover it
};

struct TravelCost {
    bool affordable = false;
    TravelCostRefusal refusal = TravelCostRefusal::None;
    std::uint64_t coin = 0;
    std::uint64_t ticks = 0;
    bool saturated = false; // a figure hit the uint64 ceiling; it is a floor, not the true value
};

TravelCost travelCost(const TravelPlan& plan, const TravelCostRates& rates, std::uint64_t purse);

// Whole world-clock ticks from a double. NaN, infinity-below and negatives give 0; anything past the uint64
// range clamps to the maximum.
std::uint64_t clampedTicks(double value);

// The world clock after travelling, saturating at the uint64 maximum.
std::uint64_t advanceClock(std::uint64_t now, std::uint64_t ticks);

// A road length from a world distance. NaN and negatives give 0; very large values clamp to uint32 max, which
// is the type TravelLink::length uses.
std::uint32_t linkLengthFromDistance(double distance);

}
