#include "ichor/stage0/visual_telegraph.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

using namespace ichor::stage0;

TEST_CASE("every shipped audio telegraph has a visual fallback", "[visual_telegraph]")
{
    const auto report = validateVisualTelegraphs(visualTelegraphTable(), audioTelegraphs());
    CHECK(report.ok());
    CHECK(report.missing.empty());
    CHECK(report.invalid.empty());
    CHECK(report.duplicates.empty());
}

TEST_CASE("every role has at least one audio telegraph", "[visual_telegraph]")
{
    for (const TelegraphRole role : {TelegraphRole::Skirmisher, TelegraphRole::Swarm, TelegraphRole::Ambusher, TelegraphRole::Anchor,
                                     TelegraphRole::Artillery, TelegraphRole::Controller}) {
        int n = 0;
        for (const auto& a : audioTelegraphs()) n += a.role == role ? 1 : 0;
        CHECK(n > 0);
    }
}

TEST_CASE("a table missing an entry is reported", "[visual_telegraph]")
{
    for (std::size_t drop = 0; drop < visualTelegraphTable().size(); ++drop) {
        std::vector<VisualTelegraph> table = visualTelegraphTable();
        const VisualTelegraph dropped = table[drop];
        table.erase(table.begin() + static_cast<std::ptrdiff_t>(drop));
        const auto report = validateVisualTelegraphs(table, audioTelegraphs());
        REQUIRE_FALSE(report.ok());
        REQUIRE(report.missing.size() == 1);
        CHECK(report.missing[0].role == dropped.role);
        CHECK(report.missing[0].attack == dropped.attack);
    }
}

TEST_CASE("an empty table reports every audio telegraph", "[visual_telegraph]")
{
    const auto report = validateVisualTelegraphs({}, audioTelegraphs());
    CHECK(report.missing.size() == audioTelegraphs().size());
}

TEST_CASE("duplicate entries are reported", "[visual_telegraph]")
{
    auto table = visualTelegraphTable();
    table.push_back(table.front());
    const auto report = validateVisualTelegraphs(table, audioTelegraphs());
    CHECK_FALSE(report.ok());
    CHECK(report.duplicates.size() == 1);
}

TEST_CASE("an entry that cannot be seen is invalid", "[visual_telegraph]")
{
    const auto bad = [](auto mutate) {
        auto table = visualTelegraphTable();
        mutate(table.front());
        return validateVisualTelegraphs(table, audioTelegraphs());
    };
    CHECK_FALSE(bad([](VisualTelegraph& v) { v.durationTicks = 0; }).ok());
    CHECK_FALSE(bad([](VisualTelegraph& v) { v.durationTicks = -5; }).ok());
    CHECK_FALSE(bad([](VisualTelegraph& v) { v.pattern = TelegraphPattern::None; }).ok());
    CHECK_FALSE(bad([](VisualTelegraph& v) { v.shape = TelegraphShape::None; }).ok());
    CHECK_FALSE(bad([](VisualTelegraph& v) { v.direction = TelegraphDirection::None; }).ok());
    CHECK_FALSE(bad([](VisualTelegraph& v) { v.shape = static_cast<TelegraphShape>(200); }).ok());
}

TEST_CASE("unknown ids are handled without crashing", "[visual_telegraph]")
{
    CHECK(findVisualTelegraph(visualTelegraphTable(), TelegraphRole::Anchor, "no_such_attack") == nullptr);
    CHECK(findVisualTelegraph(visualTelegraphTable(), TelegraphRole::Anchor, "") == nullptr);
    CHECK(findVisualTelegraph(visualTelegraphTable(), static_cast<TelegraphRole>(250), "slam") == nullptr);
    CHECK(findVisualTelegraph(visualTelegraphTable(), TelegraphRole::Anchor, "slam") != nullptr);

    TelegraphRole role{};
    CHECK(parseTelegraphRole("anchor", role));
    CHECK(role == TelegraphRole::Anchor);
    CHECK_FALSE(parseTelegraphRole("nope", role));
    CHECK_FALSE(parseTelegraphRole(std::string(100000, 'x'), role));

    const std::vector<AudioTelegraph> hostile{{static_cast<TelegraphRole>(250), "x"}, {TelegraphRole::Anchor, ""}};
    const auto report = validateVisualTelegraphs(visualTelegraphTable(), hostile);
    CHECK(report.missing.size() == 2);
}

TEST_CASE("telegraphs are distinguishable without colour", "[visual_telegraph]")
{
    const auto& table = visualTelegraphTable();
    for (std::size_t i = 0; i < table.size(); ++i)
        for (std::size_t j = i + 1; j < table.size(); ++j) {
            const bool same = table[i].shape == table[j].shape && table[i].pattern == table[j].pattern;
            INFO(table[i].attack << " vs " << table[j].attack);
            CHECK_FALSE(same);
        }
}

TEST_CASE("every off-screen-capable telegraph is flagged for an edge arrow", "[visual_telegraph]")
{
    for (const auto& v : visualTelegraphTable()) {
        INFO(v.attack);
        CHECK(v.offscreenArrow);
    }
}

TEST_CASE("off-screen bearings point the right way", "[visual_telegraph]")
{
    CHECK(offscreenBearing(0.0f, 0.0f, 10.0f, 0.0f) == 0);
    CHECK(offscreenBearing(0.0f, 0.0f, 0.0f, 10.0f) == 90);
    CHECK(offscreenBearing(0.0f, 0.0f, -10.0f, 0.0f) == 180);
    CHECK(offscreenBearing(0.0f, 0.0f, 0.0f, -10.0f) == 270);
    CHECK(offscreenBearing(1.0f, 1.0f, 1.0f, 1.0f) == 0);
    CHECK(offscreenBearing(0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f) == 0);
    CHECK(offscreenBearing(0.0f, 0.0f, std::numeric_limits<float>::infinity(), 1.0f) == 0);
    CHECK(offscreenBearing(0.0f, 0.0f, 1e30f, 1e30f) == 45);
}
