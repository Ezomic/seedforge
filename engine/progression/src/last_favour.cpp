#include "ichor/progression/last_favour.h"

namespace ichor {

LastFavourSource::LastFavourSource(std::int64_t charges)
    : charges_(charges < 0 ? 0 : (charges > kMaxFavourCharges ? kMaxFavourCharges : charges))
{
}

std::int64_t LastFavourSource::effectiveCost(std::int64_t cost) const
{
    if (cost < 1 || cost > kMaxAbilityCost)
        return 0;
    const std::int64_t scaled = cost * (1000 - discountPermille_);
    const std::int64_t rounded = (scaled + 999) / 1000;
    return rounded < 1 ? 1 : rounded;
}

std::int64_t LastFavourSource::improveEfficiency(std::int64_t permille)
{
    if (permille < 1)
        return 0;
    const std::int64_t room = kMaxFavourDiscountPermille - discountPermille_;
    const std::int64_t added = permille < room ? permille : room;
    discountPermille_ += added;
    return added;
}

bool LastFavourSource::canPay(std::int64_t cost) const
{
    const std::int64_t due = effectiveCost(cost);
    return due >= 1 && due <= charges_;
}

bool LastFavourSource::pay(std::int64_t cost)
{
    if (!canPay(cost))
        return false;
    const std::int64_t due = effectiveCost(cost);
    charges_ -= due;
    spent_ += due;
    return true;
}

}
