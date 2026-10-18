#pragma once

#include "ichor/progression/abilities.h"

#include <cstdint>

namespace ichor {

// Doc 05 / doc 18 "The Last Favour": finite charges that never refill. Progression is
// conservation: the caster does not get stronger, abilities get cheaper. This is the architecture
// test of SEED-296; it implements only the PowerSource interface of abilities.h and touches no
// ability code. Pure integer logic.

constexpr std::int64_t kMaxFavourCharges = 1000000;
// Efficiency can take at most half off a cost, so a favour is never free.
constexpr std::int64_t kMaxFavourDiscountPermille = 500;

class LastFavourSource final : public PowerSource {
public:
    // A charge count below 0 reads as 0 and above kMaxFavourCharges as kMaxFavourCharges.
    explicit LastFavourSource(std::int64_t charges);

    std::int64_t charges() const { return charges_; }
    std::int64_t spent() const { return spent_; }
    std::int64_t discountPermille() const { return discountPermille_; }

    // Charges one use of an ability of `cost` units takes: cost less the discount, rounded up,
    // never below 1. Zero when cost is outside 1 to kMaxAbilityCost.
    std::int64_t effectiveCost(std::int64_t cost) const;

    // Conservation as progression: raises the discount by `permille` (positive), clamped to
    // kMaxFavourDiscountPermille. Returns the amount actually added. There is no way to lower it
    // and no way to add charges.
    std::int64_t improveEfficiency(std::int64_t permille);

    bool canPay(std::int64_t cost) const override;
    bool pay(std::int64_t cost) override;

private:
    std::int64_t charges_ = 0;
    std::int64_t spent_ = 0;
    std::int64_t discountPermille_ = 0;
};

}
