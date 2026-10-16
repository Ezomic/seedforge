#include "ichor/traversal/travel_cost.h"

#include <limits>

namespace ichor {

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

std::uint64_t satMul(std::uint64_t a, std::uint64_t b, bool& saturated)
{
    if (a != 0 && b > kMax / a) {
        saturated = true;
        return kMax;
    }
    return a * b;
}

std::uint64_t satAdd(std::uint64_t a, std::uint64_t b, bool& saturated)
{
    if (b > kMax - a) {
        saturated = true;
        return kMax;
    }
    return a + b;
}

std::uint64_t price(std::uint64_t length, std::uint64_t hops, std::uint64_t perLength, std::uint64_t perHop, bool& saturated)
{
    return satAdd(satMul(length, perLength, saturated), satMul(hops, perHop, saturated), saturated);
}

}

TravelCost travelCost(const TravelPlan& plan, const TravelCostRates& rates, std::uint64_t purse)
{
    TravelCost out;
    if (!plan.allowed) {
        out.refusal = TravelCostRefusal::NotAllowed;
        return out;
    }
    if (plan.path.size() < 2) {
        out.refusal = TravelCostRefusal::MalformedPlan;
        return out;
    }
    const std::uint64_t hops = plan.path.size() - 1;
    out.coin = price(plan.totalLength, hops, rates.coinPerLength, rates.coinPerHop, out.saturated);
    out.ticks = price(plan.totalLength, hops, rates.ticksPerLength, rates.ticksPerHop, out.saturated);
    if (out.coin > purse) {
        out.refusal = TravelCostRefusal::InsufficientCoin;
        return out;
    }
    out.affordable = true;
    return out;
}

std::uint64_t clampedTicks(double value)
{
    if (!(value >= 1.0))
        return 0;
    if (value >= 18446744073709551616.0)
        return kMax;
    return static_cast<std::uint64_t>(value);
}

std::uint64_t advanceClock(std::uint64_t now, std::uint64_t ticks)
{
    bool saturated = false;
    return satAdd(now, ticks, saturated);
}

std::uint32_t linkLengthFromDistance(double distance)
{
    if (!(distance >= 1.0))
        return 0;
    if (distance >= 4294967296.0)
        return std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(distance);
}

}
