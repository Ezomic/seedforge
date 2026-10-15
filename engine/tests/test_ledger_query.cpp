#include "ichor/ledger/ledger_query.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ichor;

namespace {

void add(ConsequenceLedger& ledger, std::string id, std::string subject, std::string kind, std::uint64_t tick)
{
    REQUIRE(ledger.append(Fact{std::move(id), std::move(subject), std::move(kind), "1", tick}) == AppendResult::Ok);
}

TensionQuery query(std::vector<KindWeight> kinds, std::uint64_t now)
{
    TensionQuery q;
    q.kinds = std::move(kinds);
    q.now = now;
    return q;
}

}

TEST_CASE("factScore halves at the horizon and never rises with age", "[ledger_query]")
{
    CHECK(factScore(10, 100, 100, 1000) == 10000);
    CHECK(factScore(10, 0, 1000, 1000) == 5000);
    std::uint32_t last = factScore(10, 0, 0, 1000);
    for (std::uint64_t now = 1; now < 100000; now *= 3) {
        const std::uint32_t s = factScore(10, 0, now, 1000);
        CHECK(s <= last);
        last = s;
    }
}

TEST_CASE("factScore survives extreme values", "[ledger_query]")
{
    constexpr std::uint64_t max = ~std::uint64_t{0};
    CHECK(factScore(~std::uint32_t{0}, 0, 0, max) == kMaxKindWeight * 1000u);
    CHECK(factScore(kMaxKindWeight, 0, max, max) == kMaxKindWeight * 1000ull * kMaxHorizonTicks / (kMaxHorizonTicks + kMaxFactAgeTicks));
    CHECK(factScore(7, 0, max, 1000) == factScore(7, 0, kMaxFactAgeTicks, 1000));
    CHECK(factScore(1, 0, max, 0) == 0);
    CHECK(factScore(1, max, 0, 1000) == 1000);
    CHECK(factScore(0, 0, 0, 1000) == 0);
}

TEST_CASE("queryTension ranks by weight and recency", "[ledger_query]")
{
    ConsequenceLedger ledger;
    add(ledger, "a", "reach", "starving", 0);
    add(ledger, "b", "harl", "killed", 0);
    add(ledger, "c", "reach", "starving", 360000);
    add(ledger, "d", "reach", "tithe", 360000);
    const auto result = queryTension(ledger, query({{"starving", 4}, {"killed", 10}}, 360000));
    REQUIRE(result.size() == 3);
    CHECK(result[0].index == 1);
    CHECK(result[0].score == 5000);
    CHECK(result[1].index == 2);
    CHECK(result[1].score == 4000);
    CHECK(result[2].index == 0);
    CHECK(result[2].score == 2000);
}

TEST_CASE("queryTension filters by subject and ignores unlisted kinds", "[ledger_query]")
{
    ConsequenceLedger ledger;
    add(ledger, "a", "reach", "starving", 5);
    add(ledger, "b", "harl", "starving", 5);
    add(ledger, "c", "reach", "tithe", 5);
    TensionQuery q = query({{"starving", 1}}, 5);
    q.subject = "reach";
    const auto result = queryTension(ledger, q);
    REQUIRE(result.size() == 1);
    CHECK(result[0].index == 0);
    q.subject = "nobody";
    CHECK(queryTension(ledger, q).empty());
}

TEST_CASE("queryTension breaks ties by tick then index", "[ledger_query]")
{
    ConsequenceLedger ledger;
    add(ledger, "a", "x", "k", 7);
    add(ledger, "b", "x", "k", 7);
    add(ledger, "c", "x", "k", 9);
    const auto result = queryTension(ledger, query({{"k", 1}}, 9));
    REQUIRE(result.size() == 3);
    CHECK(result[0].index == 2);
    CHECK(result[1].index == 0);
    CHECK(result[2].index == 1);
}

TEST_CASE("queryTension truncates to the limit and keeps the strongest", "[ledger_query]")
{
    ConsequenceLedger ledger;
    for (std::uint64_t i = 0; i < 100; ++i) {
        add(ledger, "f" + std::to_string(i), "x", "k", i);
    }
    TensionQuery q = query({{"k", 1}}, 100);
    q.limit = 3;
    const auto result = queryTension(ledger, q);
    REQUIRE(result.size() == 3);
    CHECK(result[0].index == 99);
    CHECK(result[1].index == 98);
    CHECK(result[2].index == 97);
    q.limit = 0;
    CHECK(queryTension(ledger, q).empty());
    q.limit = ~std::size_t{0};
    CHECK(queryTension(ledger, q).size() == 100);
}

TEST_CASE("queryTension prefers the later fact when scores are equal", "[ledger_query]")
{
    ConsequenceLedger ledger;
    add(ledger, "a", "x", "k", 20);
    add(ledger, "b", "x", "k", 30);
    TensionQuery q = query({{"k", 1}}, 10);
    const auto result = queryTension(ledger, q);
    REQUIRE(result.size() == 2);
    CHECK(result[0].score == result[1].score);
    CHECK(result[0].index == 1);
    CHECK(result[1].index == 0);
}

TEST_CASE("queryTension limit is capped", "[ledger_query]")
{
    ConsequenceLedger ledger;
    for (std::uint64_t i = 0; i < kMaxQueryLimit + 50; ++i) {
        add(ledger, "f" + std::to_string(i), "x", "k", i);
    }
    TensionQuery q = query({{"k", 1}}, 1000);
    q.limit = kMaxQueryLimit + 50;
    CHECK(queryTension(ledger, q).size() == kMaxQueryLimit);
}

TEST_CASE("queryTension handles empty and hostile queries", "[ledger_query]")
{
    ConsequenceLedger ledger;
    CHECK(queryTension(ledger, query({{"k", 1}}, 0)).empty());
    add(ledger, "a", "x", "k", 0);
    CHECK(queryTension(ledger, query({}, 0)).empty());
    CHECK(queryTension(ledger, query({{"k", 0}}, 0)).empty());
    CHECK(queryTension(ledger, query({{"", 5}, {"unknown", 5}}, 0)).empty());
    const auto dup = queryTension(ledger, query({{"k", 2}, {"k", 9}, {"k", 1}}, 0));
    REQUIRE(dup.size() == 1);
    CHECK(dup[0].score == 9000);
    const auto huge = queryTension(ledger, query({{"k", ~std::uint32_t{0}}}, ~std::uint64_t{0}));
    REQUIRE(huge.size() == 1);
    CHECK(huge[0].score == factScore(kMaxKindWeight, 0, ~std::uint64_t{0}, 360000));
}

TEST_CASE("queryTension counts a fact from the future as fresh", "[ledger_query]")
{
    ConsequenceLedger ledger;
    add(ledger, "a", "x", "k", 5000);
    const auto result = queryTension(ledger, query({{"k", 3}}, 10));
    REQUIRE(result.size() == 1);
    CHECK(result[0].score == 3000);
}

TEST_CASE("queryTension is the same on every call and does not change the ledger", "[ledger_query]")
{
    ConsequenceLedger ledger;
    add(ledger, "a", "x", "k", 1);
    add(ledger, "b", "y", "k", 2);
    const std::uint64_t before = ledger.digest();
    const auto first = queryTension(ledger, query({{"k", 2}}, 50));
    const auto second = queryTension(ledger, query({{"k", 2}}, 50));
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].index == second[i].index);
        CHECK(first[i].score == second[i].score);
    }
    CHECK(ledger.digest() == before);
}
