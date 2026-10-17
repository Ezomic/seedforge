#include "ichor/ledger/quest_visible_effect.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace ichor;

namespace {

QuestEffect fx(VisibleMetric metric, const std::string& target, std::int64_t delta)
{
    return QuestEffect{metric, target, delta};
}

}

TEST_CASE("a quest with no effects is a chore", "[quest][visible]")
{
    CHECK(checkVisibleNumber({}).verdict == VisibleNumberVerdict::NoEffects);
    CHECK_FALSE(movesVisibleNumber({}));
}

TEST_CASE("each kind of visible number counts, up or down", "[quest][visible]")
{
    for (const VisibleMetric metric : {VisibleMetric::Territory, VisibleMetric::SupplyStatus, VisibleMetric::FactionStrength, VisibleMetric::Route}) {
        for (const std::int64_t delta : {1, -1}) {
            const auto report = checkVisibleNumber({fx(metric, "league", delta)});
            CHECK(report.verdict == VisibleNumberVerdict::Moves);
            CHECK(report.movedNumbers == 1);
        }
    }
}

TEST_CASE("a zero delta moves nothing", "[quest][visible]")
{
    CHECK(checkVisibleNumber({fx(VisibleMetric::Territory, "league", 0)}).verdict == VisibleNumberVerdict::AllCancel);
}

TEST_CASE("effects on the same number cancel before judging", "[quest][visible]")
{
    const auto cancelled = checkVisibleNumber({fx(VisibleMetric::Territory, "league", 3), fx(VisibleMetric::Territory, "league", -3)});
    CHECK(cancelled.verdict == VisibleNumberVerdict::AllCancel);
    CHECK(cancelled.movedNumbers == 0);

    const auto different = checkVisibleNumber({fx(VisibleMetric::Territory, "league", 3), fx(VisibleMetric::FactionStrength, "league", -3)});
    CHECK(different.verdict == VisibleNumberVerdict::Moves);
    CHECK(different.movedNumbers == 2);

    const auto partial = checkVisibleNumber({fx(VisibleMetric::Route, "strait", 2), fx(VisibleMetric::Route, "strait", -2), fx(VisibleMetric::SupplyStatus, "kessene", 1)});
    CHECK(partial.verdict == VisibleNumberVerdict::Moves);
    CHECK(partial.movedNumbers == 1);
}

TEST_CASE("bad targets and deltas are rejected with the index", "[quest][visible]")
{
    for (const std::string bad : {std::string(), std::string("League"), std::string("a b"), std::string("x\0y", 3), std::string(129, 'a'), std::string("\xff")}) {
        const auto report = checkVisibleNumber({fx(VisibleMetric::Territory, "ok", 1), fx(VisibleMetric::Territory, bad, 1)});
        CHECK(report.verdict == VisibleNumberVerdict::InvalidEffect);
        CHECK(report.badIndex == 1);
    }
    CHECK(checkVisibleNumber({fx(VisibleMetric::Territory, std::string(128, 'a'), 1)}).verdict == VisibleNumberVerdict::Moves);
    CHECK(checkVisibleNumber({fx(VisibleMetric::Territory, "a", std::numeric_limits<std::int64_t>::max())}).verdict == VisibleNumberVerdict::InvalidEffect);
    CHECK(checkVisibleNumber({fx(VisibleMetric::Territory, "a", std::numeric_limits<std::int64_t>::min())}).verdict == VisibleNumberVerdict::InvalidEffect);
    CHECK(checkVisibleNumber({fx(static_cast<VisibleMetric>(200), "a", 1)}).verdict == VisibleNumberVerdict::InvalidEffect);
}

TEST_CASE("many max-size deltas on one number cannot overflow", "[quest][visible]")
{
    const std::vector<QuestEffect> effects(kMaxQuestEffects, fx(VisibleMetric::Territory, "a", kMaxEffectDelta));
    CHECK(checkVisibleNumber(effects).verdict == VisibleNumberVerdict::Moves);
}

TEST_CASE("effect count is capped", "[quest][visible]")
{
    const std::vector<QuestEffect> effects(kMaxQuestEffects + 1, fx(VisibleMetric::Territory, "a", 1));
    CHECK(checkVisibleNumber(effects).verdict == VisibleNumberVerdict::TooManyEffects);
}

TEST_CASE("known targets are enforced when supplied", "[quest][visible]")
{
    const std::set<std::string, std::less<>> known{"league", "kessene"};
    CHECK(checkVisibleNumber({fx(VisibleMetric::Territory, "league", 1)}, &known).verdict == VisibleNumberVerdict::Moves);
    const auto report = checkVisibleNumber({fx(VisibleMetric::Territory, "league", 1), fx(VisibleMetric::Route, "ghost", 1)}, &known);
    CHECK(report.verdict == VisibleNumberVerdict::UnknownTarget);
    CHECK(report.badIndex == 1);
    const std::set<std::string, std::less<>> none;
    CHECK(checkVisibleNumber({fx(VisibleMetric::Territory, "league", 1)}, &none).verdict == VisibleNumberVerdict::UnknownTarget);
}
