#include "ichor/economy/merchant.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
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

RegionMarket floodCoast()
{
    RegionMarket market;
    market.scarce = {"grain", "timber"};
    market.abundant = {"salvage", "salt"};
    return market;
}

std::vector<MerchantOffer> stock(const std::vector<GoodDef>& goods, const RegionMarket& market, const PricingRules& rules = {})
{
    std::string error;
    const auto offers = stockMerchant(goods, market, rules, error);
    REQUIRE(offers.has_value());
    return *offers;
}

const MerchantOffer& offerFor(const std::vector<MerchantOffer>& offers, const std::string& good)
{
    const auto it = std::find_if(offers.begin(), offers.end(), [&](const MerchantOffer& o) { return o.good == good; });
    REQUIRE(it != offers.end());
    return *it;
}

}

TEST_CASE("The flooded coast pays absurdly for grain and gives away salvage")
{
    const auto offers = stock(kGoods, floodCoast());
    CHECK(offerFor(offers, "grain").price == 30);
    CHECK(offerFor(offers, "grain").stock == 25);
    CHECK(offerFor(offers, "salvage").price == 8);
    CHECK(offerFor(offers, "salvage").stock == 120);
    CHECK(offerFor(offers, "silver").price == 100);
    CHECK(offerFor(offers, "silver").stock == 10);
}

TEST_CASE("Removing the economy lists puts every price back at base")
{
    const auto offers = stock(kGoods, RegionMarket{});
    for (const GoodDef& good : kGoods) {
        CHECK(offerFor(offers, good.id).price == good.basePrice);
        CHECK(offerFor(offers, good.id).stock == good.baseStock);
    }
}

TEST_CASE("A cut route raises imported prices and thins stock, but not local abundance")
{
    RegionMarket open = floodCoast();
    RegionMarket cut = open;
    cut.route = RouteStatus::Cut;
    const auto before = stock(kGoods, open);
    const auto after = stock(kGoods, cut);

    CHECK(offerFor(after, "silver").price == 2 * offerFor(before, "silver").price);
    CHECK(offerFor(after, "silver").stock < offerFor(before, "silver").stock);
    CHECK(offerFor(after, "grain").price == 60);
    CHECK(offerFor(after, "salvage").price == offerFor(before, "salvage").price);
    CHECK(offerFor(after, "salvage").stock == offerFor(before, "salvage").stock);

    RegionMarket strained = open;
    strained.route = RouteStatus::Strained;
    const auto mid = stock(kGoods, strained);
    CHECK(offerFor(mid, "silver").price > offerFor(before, "silver").price);
    CHECK(offerFor(mid, "silver").price < offerFor(after, "silver").price);
}

TEST_CASE("A faction tariff marks up every good, abundant ones included")
{
    RegionMarket market = floodCoast();
    market.tariffPermille = 500;
    const auto offers = stock(kGoods, market);
    CHECK(offerFor(offers, "silver").price == 150);
    CHECK(offerFor(offers, "salvage").price == 12);
}

TEST_CASE("A word in both lists is scarce")
{
    RegionMarket market;
    market.scarce = {"grain"};
    market.abundant = {"grain"};
    CHECK(scarcityOf(market, "grain") == Scarcity::Scarce);
    CHECK(scarcityOf(market, "salt") == Scarcity::Normal);
}

TEST_CASE("The merchant pays only a share of the sell price")
{
    const auto offers = stock(kGoods, RegionMarket{});
    CHECK(offerFor(offers, "silver").buyback == 50);
    for (const MerchantOffer& offer : offers) {
        CHECK(offer.buyback <= offer.price);
    }
}

TEST_CASE("A tiny base price never rounds to free")
{
    const std::vector<GoodDef> goods = {{"pebble", 1, 5}};
    RegionMarket abundant;
    abundant.abundant = {"pebble"};
    PricingRules rules;
    rules.abundantPrice = 0;
    const auto offers = stock(goods, abundant, rules);
    CHECK(offers.front().price == 1);
    CHECK(offers.front().buyback <= 1);
}

TEST_CASE("Offers come out sorted by good id whatever the input order")
{
    std::vector<GoodDef> reversed(kGoods.rbegin(), kGoods.rend());
    const auto a = stock(kGoods, floodCoast());
    const auto b = stock(reversed, floodCoast());
    REQUIRE(a.size() == b.size());
    CHECK(std::is_sorted(a.begin(), a.end(), [](const MerchantOffer& x, const MerchantOffer& y) { return x.good < y.good; }));
    CHECK(offersDigest(a) == offersDigest(b));
}

TEST_CASE("The digest changes when a price or the route changes")
{
    RegionMarket cut = floodCoast();
    cut.route = RouteStatus::Cut;
    CHECK(offersDigest(stock(kGoods, floodCoast())) != offersDigest(stock(kGoods, cut)));
    CHECK(offersDigest(stock(kGoods, floodCoast())) == offersDigest(stock(kGoods, floodCoast())));
}

TEST_CASE("The market reads the merged economy block")
{
    MergedDirectives directives;
    directives.blocks["economy"]["scarce"] = std::vector<std::string>{"grain", "timber"};
    directives.blocks["economy"]["abundant"] = std::vector<std::string>{"salvage"};
    const RegionMarket market = marketFromDirectives(directives);
    CHECK(market.scarce == std::vector<std::string>{"grain", "timber"});
    CHECK(market.abundant == std::vector<std::string>{"salvage"});

    CHECK(marketFromDirectives(MergedDirectives{}).scarce.empty());

    MergedDirectives mistyped;
    mistyped.blocks["economy"]["scarce"] = 3.0;
    mistyped.blocks["economy"]["abundant"] = std::string("salt");
    const RegionMarket empty = marketFromDirectives(mistyped);
    CHECK(empty.scarce.empty());
    CHECK(empty.abundant.empty());
}

TEST_CASE("Hostile goods and rules are rejected with an error")
{
    std::string error;
    const RegionMarket market;
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

    CHECK_FALSE(stockMerchant(std::vector<GoodDef>{{"", 1, 1}}, market, {}, error).has_value());
    CHECK_FALSE(stockMerchant(std::vector<GoodDef>{{"a", 0, 1}}, market, {}, error).has_value());
    CHECK_FALSE(stockMerchant(std::vector<GoodDef>{{"a", kMax, 1}}, market, {}, error).has_value());
    CHECK_FALSE(stockMerchant(std::vector<GoodDef>{{"a", kMaxGoodPrice + 1, 1}}, market, {}, error).has_value());
    CHECK_FALSE(stockMerchant(std::vector<GoodDef>{{"a", 1, kMax}}, market, {}, error).has_value());
    CHECK_FALSE(stockMerchant(std::vector<GoodDef>{{"a", 1, 1}, {"a", 2, 2}}, market, {}, error).has_value());
    CHECK(error.find("duplicate") != std::string::npos);

    RegionMarket hugeTariff;
    hugeTariff.tariffPermille = std::numeric_limits<std::uint32_t>::max();
    CHECK_FALSE(stockMerchant(kGoods, hugeTariff, {}, error).has_value());

    PricingRules bad;
    bad.scarcePrice = std::numeric_limits<std::uint32_t>::max();
    CHECK_FALSE(stockMerchant(kGoods, market, bad, error).has_value());
    PricingRules badBuyback;
    badBuyback.buybackPrice = 1001;
    CHECK_FALSE(stockMerchant(kGoods, market, badBuyback, error).has_value());
}

TEST_CASE("Extreme legal values saturate instead of wrapping")
{
    RegionMarket market;
    market.scarce = {"gold"};
    market.route = RouteStatus::Cut;
    market.tariffPermille = kMaxPermille;
    PricingRules rules;
    rules.scarcePrice = kMaxPermille;
    rules.cutImportPrice = kMaxPermille;

    const std::vector<GoodDef> goods = {{"gold", kMaxGoodPrice, kMaxGoodStock}};
    const auto offers = stock(goods, market, rules);
    CHECK(offers.front().price == kMaxPriceResult);
    CHECK(offers.front().buyback <= offers.front().price);

    PricingRules flood;
    flood.abundantStock = kMaxPermille;
    RegionMarket abundant;
    abundant.abundant = {"gold"};
    CHECK(stock(goods, abundant, flood).front().stock <= kMaxPriceResult);
}

TEST_CASE("No goods gives no offers")
{
    CHECK(stock({}, floodCoast()).empty());
}
