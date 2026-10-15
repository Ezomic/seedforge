#include "ichor/economy/trade_routes.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::vector<GoodDef> kGoods = {
    {"grain", 10, 100},
    {"salvage", 20, 40},
    {"timber", 8, 60},
    {"silver", 100, 10},
};

const std::vector<GoodSource> kSources = {
    {"grain", "river"},
    {"timber", "bridge"},
    {"silver", "river"},
};

RegionMarket market()
{
    RegionMarket m;
    m.abundant = {"salvage"};
    return m;
}

std::vector<MerchantOffer> priced(const std::vector<TradeRoute>& routes, const std::vector<GoodSource>& sources = kSources)
{
    std::string error;
    const auto offers = stockMerchantViaRoutes(kGoods, market(), routes, sources, {}, error);
    REQUIRE(offers.has_value());
    return *offers;
}

const MerchantOffer& offerFor(const std::vector<MerchantOffer>& offers, const std::string& good)
{
    const auto it = std::find_if(offers.begin(), offers.end(), [&](const MerchantOffer& o) { return o.good == good; });
    REQUIRE(it != offers.end());
    return *it;
}

bool fails(const std::vector<GoodDef>& goods, const std::vector<TradeRoute>& routes, const std::vector<GoodSource>& sources)
{
    std::string error;
    const bool failed = !stockMerchantViaRoutes(goods, market(), routes, sources, {}, error).has_value();
    return failed && !error.empty();
}

}

TEST_CASE("open routes leave prices at the plain merchant result", "[economy][routes]")
{
    const auto open = priced({{"river", RouteStatus::Open}, {"bridge", RouteStatus::Open}});
    std::string error;
    const auto plain = stockMerchant(kGoods, market(), {}, error);
    REQUIRE(plain.has_value());
    CHECK(offersDigest(open) == offersDigest(*plain));
}

TEST_CASE("cutting a route raises only the goods that arrive by it", "[economy][routes]")
{
    const auto open = priced({{"river", RouteStatus::Open}, {"bridge", RouteStatus::Open}});
    const auto cut = priced({{"river", RouteStatus::Cut}, {"bridge", RouteStatus::Open}});

    CHECK(offerFor(cut, "grain").price == offerFor(open, "grain").price * 2);
    CHECK(offerFor(cut, "silver").price == offerFor(open, "silver").price * 2);
    CHECK(offerFor(cut, "grain").stock < offerFor(open, "grain").stock);
    CHECK(offerFor(cut, "timber").price == offerFor(open, "timber").price);
    CHECK(offerFor(cut, "timber").stock == offerFor(open, "timber").stock);
}

TEST_CASE("a strained route sits between open and cut", "[economy][routes]")
{
    const auto open = priced({{"river", RouteStatus::Open}, {"bridge", RouteStatus::Open}});
    const auto strained = priced({{"river", RouteStatus::Strained}, {"bridge", RouteStatus::Open}});
    const auto cut = priced({{"river", RouteStatus::Cut}, {"bridge", RouteStatus::Open}});
    CHECK(offerFor(open, "grain").price < offerFor(strained, "grain").price);
    CHECK(offerFor(strained, "grain").price < offerFor(cut, "grain").price);
}

TEST_CASE("goods with no source follow the market route; abundant goods ignore routes", "[economy][routes]")
{
    RegionMarket m = market();
    m.route = RouteStatus::Cut;
    std::string error;
    const std::vector<TradeRoute> open = {{"river", RouteStatus::Open}, {"bridge", RouteStatus::Open}};
    const auto offers = stockMerchantViaRoutes(kGoods, m, open, kSources, {}, error);
    REQUIRE(offers.has_value());
    const auto plainCut = stockMerchant(kGoods, m, {}, error);
    REQUIRE(plainCut.has_value());
    CHECK(offerFor(*offers, "salvage").price == offerFor(*plainCut, "salvage").price);
    CHECK(offerFor(*offers, "grain").price < offerFor(*plainCut, "grain").price);

    const auto noSources = priced({{"river", RouteStatus::Cut}}, {});
    CHECK(offerFor(noSources, "grain").price == offerFor(priced({{"river", RouteStatus::Open}}, {}), "grain").price);
    CHECK(offerFor(priced({{"river", RouteStatus::Cut}, {"bridge", RouteStatus::Open}}), "salvage").price
        == offerFor(priced({{"river", RouteStatus::Open}, {"bridge", RouteStatus::Open}}), "salvage").price);
}

TEST_CASE("output does not depend on input order", "[economy][routes]")
{
    auto reversedGoods = kGoods;
    std::reverse(reversedGoods.begin(), reversedGoods.end());
    auto reversedSources = kSources;
    std::reverse(reversedSources.begin(), reversedSources.end());
    const std::vector<TradeRoute> routes = {{"river", RouteStatus::Cut}, {"bridge", RouteStatus::Strained}};
    const std::vector<TradeRoute> reversedRoutes = {routes[1], routes[0]};
    std::string error;
    const auto a = stockMerchantViaRoutes(kGoods, market(), routes, kSources, {}, error);
    const auto b = stockMerchantViaRoutes(reversedGoods, market(), reversedRoutes, reversedSources, {}, error);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(offersDigest(*a) == offersDigest(*b));
}

TEST_CASE("hostile route input is rejected with an error", "[economy][routes]")
{
    const std::vector<TradeRoute> routes = {{"river", RouteStatus::Open}};
    CHECK(fails(kGoods, {{"river", RouteStatus::Open}, {"river", RouteStatus::Cut}}, {}));
    CHECK(fails(kGoods, {{"", RouteStatus::Open}}, {}));
    CHECK(fails(kGoods, routes, {{"grain", "nowhere"}}));
    CHECK(fails(kGoods, routes, {{"grain", "river"}, {"grain", "river"}}));
    CHECK(fails(kGoods, routes, {{"", "river"}}));
    CHECK(fails({{"grain", 0, 1}}, routes, {}));
    CHECK(fails({{"grain", 1, 1}, {"grain", 1, 1}}, routes, {}));
}

TEST_CASE("empty goods and unused sources are fine", "[economy][routes]")
{
    std::string error;
    const std::vector<GoodDef> noGoods;
    const std::vector<TradeRoute> cut = {{"river", RouteStatus::Cut}};
    const std::vector<GoodSource> iron = {{"iron", "river"}};
    const auto none = stockMerchantViaRoutes(noGoods, market(), {}, {}, {}, error);
    REQUIRE(none.has_value());
    CHECK(none->empty());
    CHECK(stockMerchantViaRoutes(noGoods, market(), cut, iron, {}, error).has_value());
}
