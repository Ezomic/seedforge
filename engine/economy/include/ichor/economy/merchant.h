#pragma once

#include "ichor/data/directive_merge.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 06 (Merchants): a region's stock and prices come from its axiom economy
// lists (scarce, abundant), who controls it, and whether its trade route is
// open. All arithmetic is integer permille (1000 = x1.0), so the result is
// identical on every platform and never touches a float.

constexpr std::uint64_t kMaxGoodPrice = 1000000000ull;        // a base price above this is rejected
constexpr std::uint64_t kMaxPriceResult = 1000000000000ull;   // derived prices saturate here
constexpr std::uint64_t kMaxGoodStock = 1000000ull;
constexpr std::uint32_t kMaxPermille = 100000;

struct GoodDef {
    std::string id;
    std::uint64_t basePrice = 0; // coin, 1 to kMaxGoodPrice
    std::uint64_t baseStock = 0; // units a normal market holds, 0 to kMaxGoodStock
};

// The state of the trade route feeding a region. A cut route (the player sank
// the hull, burnt the bridge) raises the price of imported goods; goods the
// region itself has in abundance do not travel and are unaffected.
enum class RouteStatus : std::uint8_t { Open, Strained, Cut };

struct RegionMarket {
    std::vector<std::string> scarce;
    std::vector<std::string> abundant;
    RouteStatus route = RouteStatus::Open;
    // Markup from whoever controls the region, in permille above 1000 (0 = none).
    std::uint32_t tariffPermille = 0;
};

// Every multiplier is permille.
struct PricingRules {
    std::uint32_t scarcePrice = 3000;
    std::uint32_t abundantPrice = 400;
    std::uint32_t strainedImportPrice = 1250;
    std::uint32_t cutImportPrice = 2000;
    std::uint32_t scarceStock = 250;
    std::uint32_t abundantStock = 3000;
    std::uint32_t strainedImportStock = 750;
    std::uint32_t cutImportStock = 250;
    std::uint32_t buybackPrice = 500; // what the merchant pays, as a share of the sell price
};

// The merged world economy block as a market with an open route and no tariff.
// Missing or mistyped lists give an empty market.
RegionMarket marketFromDirectives(const MergedDirectives& directives);

enum class Scarcity : std::uint8_t { Abundant, Normal, Scarce };

// A word in both lists counts as scarce: the cautious reading.
Scarcity scarcityOf(const RegionMarket& market, std::string_view good);

struct MerchantOffer {
    std::string good;
    Scarcity scarcity = Scarcity::Normal;
    std::uint64_t price = 0;   // what the player pays, at least 1
    std::uint64_t buyback = 0; // what the merchant pays the player
    std::uint64_t stock = 0;
};

// One offer per good, sorted by good id, so the order never depends on input
// order. Nothing (with `error` set) for a duplicate or empty good id, a zero or
// oversize base price or stock, a tariff or rule above kMaxPermille, or a
// buyback share above 1000.
std::optional<std::vector<MerchantOffer>> stockMerchant(std::span<const GoodDef> goods, const RegionMarket& market, const PricingRules& rules, std::string& error);

// FNV-1a over the offers in order; identical on every platform.
std::uint64_t offersDigest(std::span<const MerchantOffer> offers);

}
