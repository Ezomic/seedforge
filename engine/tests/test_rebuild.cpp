#include "ichor/data/data_set.h"
#include "ichor/economy/rebuild.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

CurrencyTable shipped()
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    const DataTable* table = data.table("currency");
    REQUIRE(table != nullptr);
    std::string error;
    auto result = CurrencyTable::fromTable(*table, error);
    INFO(error);
    REQUIRE(result.has_value());
    return *result;
}

Wallet richWallet(const CurrencyTable& table, std::uint64_t coin)
{
    Wallet wallet;
    REQUIRE(wallet.earn(table, CurrencySlot::Coin, coin, "loot").credited == coin);
    return wallet;
}

RebuildBoard board()
{
    const std::vector<RebuildSite> sites = {
        {"hamlet", SiteKind::Settlement, SiteCondition::Ruined},
        {"old_bridge", SiteKind::Bridge, SiteCondition::Damaged},
        {"salt_road", SiteKind::Route, SiteCondition::Ruined},
        {"market", SiteKind::Settlement, SiteCondition::Restored},
    };
    std::string error;
    auto result = RebuildBoard::create(sites, error);
    INFO(error);
    REQUIRE(result.has_value());
    return *result;
}

}

TEST_CASE("the shipped currency table lets coin pay for rebuilding")
{
    const CurrencyTable table = shipped();
    CHECK(table.canSpendOn(CurrencySlot::Coin, "rebuild"));
    CHECK_FALSE(table.canSpendOn(CurrencySlot::Hard, "rebuild"));
}

TEST_CASE("funding a project draws coin, moves progress and restores the site on completion")
{
    const CurrencyTable table = shipped();
    Wallet wallet = richWallet(table, 1000);
    RebuildBoard b = board();

    std::uint32_t id = 0;
    REQUIRE(b.start("hamlet", SiteCondition::Restored, 300, id) == StartStatus::Ok);

    const FundResult first = b.fund(wallet, table, id, 100);
    CHECK(first.status == FundStatus::Ok);
    CHECK(first.spent == 100);
    CHECK_FALSE(first.completed);
    CHECK(wallet.balance(CurrencySlot::Coin) == 900);
    CHECK(b.project(id)->funded == 100);
    CHECK(b.condition("hamlet") == SiteCondition::Ruined);

    const FundResult last = b.fund(wallet, table, id, 200);
    CHECK(last.completed);
    CHECK(wallet.balance(CurrencySlot::Coin) == 700);
    CHECK(b.condition("hamlet") == SiteCondition::Restored);
    CHECK(b.project(id)->done);
}

TEST_CASE("insufficient funds change nothing")
{
    const CurrencyTable table = shipped();
    Wallet wallet = richWallet(table, 50);
    RebuildBoard b = board();
    std::uint32_t id = 0;
    REQUIRE(b.start("hamlet", SiteCondition::Restored, 300, id) == StartStatus::Ok);

    const Wallet before = wallet;
    CHECK(b.fund(wallet, table, id, 51).status == FundStatus::Insufficient);
    CHECK(wallet == before);
    CHECK(b.project(id)->funded == 0);
    CHECK(b.condition("hamlet") == SiteCondition::Ruined);
    CHECK(b.fund(wallet, table, id, 50).status == FundStatus::Ok);
    CHECK(wallet.balance(CurrencySlot::Coin) == 0);
}

TEST_CASE("overpaying charges only what the project still needs")
{
    const CurrencyTable table = shipped();
    Wallet wallet = richWallet(table, 1000);
    RebuildBoard b = board();
    std::uint32_t id = 0;
    REQUIRE(b.start("salt_road", SiteCondition::Restored, 120, id) == StartStatus::Ok);

    REQUIRE(b.fund(wallet, table, id, 100).status == FundStatus::Ok);
    const FundResult over = b.fund(wallet, table, id, kMax);
    CHECK(over.status == FundStatus::Ok);
    CHECK(over.spent == 20);
    CHECK(over.completed);
    CHECK(wallet.balance(CurrencySlot::Coin) == 880);
    CHECK(b.fund(wallet, table, id, 5).status == FundStatus::AlreadyDone);
    CHECK(wallet.balance(CurrencySlot::Coin) == 880);
}

TEST_CASE("an overpay is clamped to the remainder before the wallet is checked")
{
    const CurrencyTable table = shipped();
    Wallet wallet = richWallet(table, 30);
    RebuildBoard b = board();
    std::uint32_t id = 0;
    REQUIRE(b.start("hamlet", SiteCondition::Restored, 25, id) == StartStatus::Ok);
    CHECK(b.fund(wallet, table, id, 1000).spent == 25);
    CHECK(wallet.balance(CurrencySlot::Coin) == 5);
}

TEST_CASE("a route project moves the route status from cut to strained to open")
{
    const CurrencyTable table = shipped();
    Wallet wallet = richWallet(table, 1000);
    RebuildBoard b = board();
    CHECK(b.routeStatus("salt_road") == RouteStatus::Cut);

    std::uint32_t id = 0;
    REQUIRE(b.start("salt_road", SiteCondition::Damaged, 100, id) == StartStatus::Ok);
    REQUIRE(b.fund(wallet, table, id, 100).completed);
    CHECK(b.routeStatus("salt_road") == RouteStatus::Strained);

    REQUIRE(b.start("salt_road", SiteCondition::Restored, 200, id) == StartStatus::Ok);
    REQUIRE(b.fund(wallet, table, id, 200).completed);
    CHECK(b.routeStatus("salt_road") == RouteStatus::Open);
    CHECK(b.projects().size() == 2);
}

TEST_CASE("only routes have a route status")
{
    const RebuildBoard b = board();
    CHECK_FALSE(b.routeStatus("hamlet").has_value());
    CHECK_FALSE(b.routeStatus("old_bridge").has_value());
    CHECK_FALSE(b.routeStatus("nowhere").has_value());
}

TEST_CASE("projects that make no improvement are refused")
{
    RebuildBoard b = board();
    std::uint32_t id = 0;
    CHECK(b.start("market", SiteCondition::Restored, 10, id) == StartStatus::NotAnImprovement);
    CHECK(b.start("old_bridge", SiteCondition::Damaged, 10, id) == StartStatus::NotAnImprovement);
    CHECK(b.start("old_bridge", SiteCondition::Ruined, 10, id) == StartStatus::NotAnImprovement);
    CHECK(b.projects().empty());
}

TEST_CASE("a site takes one project at a time and a finished one frees it")
{
    const CurrencyTable table = shipped();
    Wallet wallet = richWallet(table, 1000);
    RebuildBoard b = board();
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    REQUIRE(b.start("hamlet", SiteCondition::Damaged, 50, first) == StartStatus::Ok);
    CHECK(b.start("hamlet", SiteCondition::Restored, 80, second) == StartStatus::SiteBusy);
    REQUIRE(b.fund(wallet, table, first, 50).completed);
    CHECK(b.start("hamlet", SiteCondition::Restored, 80, second) == StartStatus::Ok);
    CHECK(second != first);
}

TEST_CASE("concurrent projects on different sites fund independently from one wallet")
{
    const CurrencyTable table = shipped();
    Wallet wallet = richWallet(table, 150);
    RebuildBoard b = board();
    std::uint32_t hamlet = 0;
    std::uint32_t bridge = 0;
    REQUIRE(b.start("hamlet", SiteCondition::Restored, 100, hamlet) == StartStatus::Ok);
    REQUIRE(b.start("old_bridge", SiteCondition::Restored, 100, bridge) == StartStatus::Ok);

    CHECK(b.fund(wallet, table, hamlet, 100).completed);
    CHECK(b.fund(wallet, table, bridge, 100).status == FundStatus::Insufficient);
    CHECK(b.fund(wallet, table, bridge, 50).status == FundStatus::Ok);
    CHECK(b.condition("hamlet") == SiteCondition::Restored);
    CHECK(b.condition("old_bridge") == SiteCondition::Damaged);
    CHECK(b.project(bridge)->funded == 50);
    CHECK(wallet.balance(CurrencySlot::Coin) == 0);
}

TEST_CASE("an unknown project and a zero fund are handled")
{
    const CurrencyTable table = shipped();
    Wallet wallet = richWallet(table, 100);
    RebuildBoard b = board();
    CHECK(b.fund(wallet, table, 7, 10).status == FundStatus::UnknownProject);
    CHECK(b.project(7) == nullptr);

    std::uint32_t id = 0;
    REQUIRE(b.start("hamlet", SiteCondition::Restored, 10, id) == StartStatus::Ok);
    const FundResult zero = b.fund(wallet, table, id, 0);
    CHECK(zero.status == FundStatus::Ok);
    CHECK(zero.spent == 0);
    CHECK_FALSE(zero.completed);
    CHECK(wallet.balance(CurrencySlot::Coin) == 100);
}

TEST_CASE("costs are bounded so progress arithmetic cannot overflow")
{
    RebuildBoard b = board();
    std::uint32_t id = 0;
    CHECK(b.start("hamlet", SiteCondition::Restored, 0, id) == StartStatus::BadCost);
    CHECK(b.start("hamlet", SiteCondition::Restored, kMax, id) == StartStatus::BadCost);
    CHECK(b.start("hamlet", SiteCondition::Restored, kMaxCurrencyCap + 1, id) == StartStatus::BadCost);
    CHECK(b.start("hamlet", SiteCondition::Restored, kMaxCurrencyCap, id) == StartStatus::Ok);

    const CurrencyTable table = shipped();
    Wallet wallet = richWallet(table, 999999999);
    CHECK(b.fund(wallet, table, id, kMax).status == FundStatus::Insufficient);
    CHECK(b.project(id)->funded == 0);
    CHECK(b.fund(wallet, table, id, 999999999).spent == 999999999);
    CHECK(b.project(id)->funded == 999999999);
    CHECK_FALSE(b.project(id)->done);
}

TEST_CASE("hostile site ids are refused without effect")
{
    RebuildBoard b = board();
    std::uint32_t id = 0;
    CHECK(b.start("", SiteCondition::Restored, 10, id) == StartStatus::UnknownSite);
    CHECK(b.start(std::string(100000, 'x'), SiteCondition::Restored, 10, id) == StartStatus::UnknownSite);
    CHECK(b.start(std::string("ham\0let", 7), SiteCondition::Restored, 10, id) == StartStatus::UnknownSite);
    CHECK(b.start("HAMLET", SiteCondition::Restored, 10, id) == StartStatus::UnknownSite);
    CHECK_FALSE(b.condition("../../etc").has_value());
    CHECK(b.projects().empty());
    CHECK(b.project(std::numeric_limits<std::uint32_t>::max()) == nullptr);
}

TEST_CASE("a board refuses hostile or duplicate site ids")
{
    std::string error;
    const std::vector<std::string> bad = {"", std::string(65, 'a'), std::string("a\0b", 3), "a b", "a\nb", "\xff"};
    for (const std::string& name : bad) {
        error.clear();
        const std::vector<RebuildSite> sites = {{name, SiteKind::Bridge, SiteCondition::Ruined}};
        CHECK_FALSE(RebuildBoard::create(sites, error).has_value());
        CHECK_FALSE(error.empty());
    }
    const std::vector<RebuildSite> twice = {{"a", SiteKind::Bridge, SiteCondition::Ruined}, {"a", SiteKind::Route, SiteCondition::Ruined}};
    CHECK_FALSE(RebuildBoard::create(twice, error).has_value());
    const std::vector<RebuildSite> longest = {{std::string(64, 'a'), SiteKind::Bridge, SiteCondition::Ruined}};
    CHECK(RebuildBoard::create(longest, error).has_value());
}

TEST_CASE("the site count is bounded")
{
    std::vector<RebuildSite> sites;
    for (std::size_t i = 0; i <= kMaxRebuildSites; ++i) {
        sites.push_back({"s" + std::to_string(i), SiteKind::Settlement, SiteCondition::Ruined});
    }
    std::string error;
    CHECK_FALSE(RebuildBoard::create(sites, error).has_value());
    sites.pop_back();
    CHECK(RebuildBoard::create(sites, error).has_value());
}

TEST_CASE("an out of range enum value is refused")
{
    RebuildBoard b = board();
    std::uint32_t id = 0;
    CHECK(b.start("hamlet", static_cast<SiteCondition>(200), 10, id) == StartStatus::NotAnImprovement);
    std::string error;
    const std::vector<RebuildSite> kind = {{"a", static_cast<SiteKind>(9), SiteCondition::Ruined}};
    CHECK_FALSE(RebuildBoard::create(kind, error).has_value());
    const std::vector<RebuildSite> condition = {{"a", SiteKind::Bridge, static_cast<SiteCondition>(9)}};
    CHECK_FALSE(RebuildBoard::create(condition, error).has_value());
}
