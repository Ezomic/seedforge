#include "ichor/economy/merchant.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <array>
#include <variant>

namespace ichor {

namespace {

std::vector<std::string> wordList(const AxiomBlock& block, std::string_view key)
{
    const auto it = block.find(key);
    if (it == block.end()) {
        return {};
    }
    const auto* words = std::get_if<std::vector<std::string>>(&it->second);
    return words ? *words : std::vector<std::string>{};
}

bool listed(const std::vector<std::string>& words, std::string_view good)
{
    return std::find(words.begin(), words.end(), good) != words.end();
}

// value * permille / 1000, rounded half up, saturating at kMaxPriceResult. The
// inputs are bounded, so the product cannot wrap.
std::uint64_t scale(std::uint64_t value, std::uint32_t permille)
{
    const std::uint64_t product = std::min(value, kMaxPriceResult) * permille;
    return std::min((product + 500) / 1000, kMaxPriceResult);
}

bool validRules(const PricingRules& rules)
{
    const std::array<std::uint32_t, 8> multipliers = {rules.scarcePrice, rules.abundantPrice, rules.strainedImportPrice, rules.cutImportPrice,
        rules.scarceStock, rules.abundantStock, rules.strainedImportStock, rules.cutImportStock};
    return std::all_of(multipliers.begin(), multipliers.end(), [](std::uint32_t m) { return m <= kMaxPermille; }) && rules.buybackPrice <= 1000;
}

std::uint32_t routePriceMult(RouteStatus route, const PricingRules& rules)
{
    switch (route) {
    case RouteStatus::Strained: return rules.strainedImportPrice;
    case RouteStatus::Cut: return rules.cutImportPrice;
    case RouteStatus::Open: break;
    }
    return 1000;
}

std::uint32_t routeStockMult(RouteStatus route, const PricingRules& rules)
{
    switch (route) {
    case RouteStatus::Strained: return rules.strainedImportStock;
    case RouteStatus::Cut: return rules.cutImportStock;
    case RouteStatus::Open: break;
    }
    return 1000;
}

void appendU64(std::string& out, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

}

RegionMarket marketFromDirectives(const MergedDirectives& directives)
{
    RegionMarket market;
    const auto it = directives.blocks.find("economy");
    if (it == directives.blocks.end()) {
        return market;
    }
    market.scarce = wordList(it->second, "scarce");
    market.abundant = wordList(it->second, "abundant");
    return market;
}

Scarcity scarcityOf(const RegionMarket& market, std::string_view good)
{
    if (listed(market.scarce, good)) {
        return Scarcity::Scarce;
    }
    return listed(market.abundant, good) ? Scarcity::Abundant : Scarcity::Normal;
}

std::optional<std::vector<MerchantOffer>> stockMerchant(std::span<const GoodDef> goods, const RegionMarket& market, const PricingRules& rules, std::string& error)
{
    if (!validRules(rules)) {
        error = "pricing rules: a multiplier is out of range";
        return std::nullopt;
    }
    if (market.tariffPermille > kMaxPermille) {
        error = "market: tariff is out of range";
        return std::nullopt;
    }

    std::vector<const GoodDef*> sorted;
    sorted.reserve(goods.size());
    for (const GoodDef& good : goods) {
        if (good.id.empty()) {
            error = "good: empty id";
            return std::nullopt;
        }
        if (good.basePrice == 0 || good.basePrice > kMaxGoodPrice) {
            error = "good " + good.id + ": base price must be 1 to " + std::to_string(kMaxGoodPrice);
            return std::nullopt;
        }
        if (good.baseStock > kMaxGoodStock) {
            error = "good " + good.id + ": base stock is over " + std::to_string(kMaxGoodStock);
            return std::nullopt;
        }
        sorted.push_back(&good);
    }
    std::sort(sorted.begin(), sorted.end(), [](const GoodDef* a, const GoodDef* b) { return a->id < b->id; });
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        if (sorted[i]->id == sorted[i - 1]->id) {
            error = "good " + sorted[i]->id + ": duplicate id";
            return std::nullopt;
        }
    }

    std::vector<MerchantOffer> offers;
    offers.reserve(sorted.size());
    for (const GoodDef* good : sorted) {
        MerchantOffer offer;
        offer.good = good->id;
        offer.scarcity = scarcityOf(market, good->id);

        std::uint32_t priceMult = 1000;
        std::uint32_t stockMult = 1000;
        switch (offer.scarcity) {
        case Scarcity::Scarce:
            priceMult = rules.scarcePrice;
            stockMult = rules.scarceStock;
            break;
        case Scarcity::Abundant:
            priceMult = rules.abundantPrice;
            stockMult = rules.abundantStock;
            break;
        case Scarcity::Normal:
            break;
        }

        std::uint64_t price = scale(good->basePrice, priceMult);
        std::uint64_t stock = scale(good->baseStock, stockMult);
        if (offer.scarcity != Scarcity::Abundant) {
            price = scale(price, routePriceMult(market.route, rules));
            stock = scale(stock, routeStockMult(market.route, rules));
        }
        price = scale(price, 1000 + market.tariffPermille);

        offer.price = std::max<std::uint64_t>(price, 1);
        offer.buyback = scale(offer.price, rules.buybackPrice);
        offer.stock = stock;
        offers.push_back(std::move(offer));
    }
    return offers;
}

std::uint64_t offersDigest(std::span<const MerchantOffer> offers)
{
    std::string bytes;
    for (const MerchantOffer& offer : offers) {
        appendU64(bytes, offer.good.size());
        bytes += offer.good;
        appendU64(bytes, static_cast<std::uint64_t>(offer.scarcity));
        appendU64(bytes, offer.price);
        appendU64(bytes, offer.buyback);
        appendU64(bytes, offer.stock);
    }
    return hash64(bytes);
}

}
