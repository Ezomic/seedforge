#include "ichor/economy/trade_routes.h"

#include <algorithm>
#include <map>

namespace ichor {

std::optional<std::vector<MerchantOffer>> stockMerchantViaRoutes(std::span<const GoodDef> goods, const RegionMarket& market,
    std::span<const TradeRoute> routes, std::span<const GoodSource> sources, const PricingRules& rules, std::string& error)
{
    std::map<std::string, RouteStatus> statusById;
    for (const TradeRoute& route : routes) {
        if (route.id.empty()) {
            error = "route: empty id";
            return std::nullopt;
        }
        if (!statusById.emplace(route.id, route.status).second) {
            error = "route " + route.id + ": duplicate id";
            return std::nullopt;
        }
    }

    std::map<std::string, RouteStatus> statusByGood;
    for (const GoodSource& source : sources) {
        if (source.good.empty()) {
            error = "source: empty good id";
            return std::nullopt;
        }
        const auto route = statusById.find(source.route);
        if (route == statusById.end()) {
            error = "source " + source.good + ": unknown route " + source.route;
            return std::nullopt;
        }
        if (!statusByGood.emplace(source.good, route->second).second) {
            error = "source " + source.good + ": listed twice";
            return std::nullopt;
        }
    }

    auto offers = stockMerchant(goods, market, rules, error);
    if (!offers) {
        return std::nullopt;
    }
    for (MerchantOffer& offer : *offers) {
        const auto source = statusByGood.find(offer.good);
        if (source == statusByGood.end() || source->second == market.route) {
            continue;
        }
        const auto good = std::find_if(goods.begin(), goods.end(), [&](const GoodDef& g) { return g.id == offer.good; });
        RegionMarket viaRoute = market;
        viaRoute.route = source->second;
        const GoodDef single[] = {*good};
        auto repriced = stockMerchant(single, viaRoute, rules, error);
        if (!repriced) {
            return std::nullopt;
        }
        offer = std::move(repriced->front());
    }
    return offers;
}

}
