#pragma once

#include "ichor/economy/merchant.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ichor {

// Doc 06: cutting a trade route moves prices. A region can be fed by several
// routes, and each imported good arrives by one of them, so sinking one hull
// raises only the goods that travelled on it.

struct TradeRoute {
    std::string id;
    RouteStatus status = RouteStatus::Open;
};

struct GoodSource {
    std::string good;
    std::string route;
};

// stockMerchant with a route status per good. A good listed in `sources` is
// priced with the status of its route; any other good uses market.route. A
// source naming a good the merchant does not stock is ignored. Nothing (with
// `error` set) for an empty or duplicate route id, a source with an empty or
// duplicate good or an unknown route, or any input stockMerchant rejects.
std::optional<std::vector<MerchantOffer>> stockMerchantViaRoutes(std::span<const GoodDef> goods, const RegionMarket& market,
    std::span<const TradeRoute> routes, std::span<const GoodSource> sources, const PricingRules& rules, std::string& error);

}
