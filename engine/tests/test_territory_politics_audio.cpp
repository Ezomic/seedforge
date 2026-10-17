#include "ichor/audio/territory_politics_audio.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr std::uint8_t N = kTerritoryAudioNoOwner;

// 12x1 strip, cell size 10: faction 0 owns x 0..5 (world 0..60), faction 1 owns x 6..11.
struct Strip {
    std::vector<std::uint8_t> owner = std::vector<std::uint8_t>(12, 0);
    std::vector<std::uint8_t> rival = std::vector<std::uint8_t>(12, N);
    Strip()
    {
        for (int i = 6; i < 12; ++i) {
            owner[i] = 1;
        }
    }
    std::optional<TerritoryAudioGrid> grid(const TerritoryPoliticsParams& p = {}) const
    {
        return territoryGridFromPolitics(owner, rival, 12, 1, 2, 10.0f, 0.0f, 0.0f, p);
    }
};

std::vector<TerritoryAudioLayer> layers()
{
    return {{"compact_bell", 1.0f}, {"garrison_horn", 1.0f}};
}

TerritoryAudioMix mixAt(const TerritoryAudioGrid& g, float x)
{
    return territoryAudioMix(g, layers(), x, 5.0f);
}

TerritoryAudioMix fake(float a, float b)
{
    TerritoryAudioMix m;
    m.gains = {a, b};
    return m;
}

}  // namespace

TEST_CASE("a border is audible before it is visible", "[territory_politics]")
{
    const Strip s;
    TerritoryPoliticsParams off;
    off.bleedCells = 0;
    const auto without = s.grid(off);
    const auto with = s.grid();
    REQUIRE(without);
    REQUIRE(with);
    // x = 55 is the centre of the last faction-0 cell, one cell short of the border at x = 60.
    const float x = 55.0f;
    CHECK(mixAt(*without, x).gains[1] < 0.05f);
    CHECK(mixAt(*with, x).gains[1] > 0.3f);
    CHECK(mixAt(*with, x).dominant == 0);
}

TEST_CASE("deep inside a territory only its own layer plays, at full gain", "[territory_politics]")
{
    const Strip s;
    const auto g = s.grid();
    REQUIRE(g);
    CHECK(mixAt(*g, 5.0f).gains[0] == Approx(1.0f));
    CHECK(mixAt(*g, 5.0f).gains[1] == Approx(0.0f));
    CHECK(mixAt(*g, 115.0f).gains[1] == Approx(1.0f));
    CHECK(mixAt(*g, 115.0f).gains[0] == Approx(0.0f));
}

TEST_CASE("gains cross a border continuously, stay bounded and really cross-fade", "[territory_politics]")
{
    const Strip s;
    const auto g = s.grid();
    REQUIRE(g);
    float prev = mixAt(*g, 0.0f).gains[0];
    float maxJump = 0.0f;
    float maxSum = 0.0f;
    float meanMin = 0.0f;
    int n = 0;
    for (float x = 0.0f; x <= 120.0f; x += 0.05f) {
        const TerritoryAudioMix m = mixAt(*g, x);
        maxJump = std::max(maxJump, std::fabs(m.gains[0] - prev));
        prev = m.gains[0];
        maxSum = std::max(maxSum, m.gains[0] + m.gains[1]);
        if (x > 50.0f && x < 70.0f) {
            meanMin += std::min(m.gains[0], m.gains[1]);
            ++n;
        }
    }
    CHECK(maxJump < 0.01f);
    CHECK(maxSum <= 1.0001f);
    CHECK(meanMin / static_cast<float>(n) > 0.15f);
}

TEST_CASE("contested cells split the loudness between owner and rival", "[territory_politics]")
{
    Strip s;
    s.rival[5] = 1;
    TerritoryPoliticsParams p;
    p.bleedCells = 0;
    const auto g = s.grid(p);
    REQUIRE(g);
    CHECK(g->strength[0][5] == 600);
    CHECK(g->strength[1][5] == 400);
    CHECK(g->strength[0][4] == 1000);
    CHECK(g->strength[1][4] == 0);
}

TEST_CASE("a layer with no id stays silent, and unowned land makes no sound", "[territory_politics]")
{
    Strip s;
    for (int i = 0; i < 12; ++i) {
        s.owner[i] = i < 6 ? 0 : N;
    }
    const auto g = s.grid();
    REQUIRE(g);
    const std::vector<TerritoryAudioLayer> renouncer = {{"compact_bell", 1.0f}, {"", 1.0f}};
    const TerritoryAudioMix m = territoryAudioMix(*g, renouncer, 115.0f, 5.0f);
    CHECK(m.gains[0] == Approx(0.0f));
    CHECK(m.gains[1] == Approx(0.0f));
    CHECK(m.dominant == -1);
}

TEST_CASE("the same politics map gives the identical grid", "[territory_politics]")
{
    const Strip s;
    const auto a = s.grid();
    const auto b = s.grid();
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->strength == b->strength);
}

TEST_CASE("hostile politics input is rejected without crashing", "[territory_politics]")
{
    const Strip s;
    const auto build = [&](std::uint32_t w, std::uint32_t h, int fc, float cs = 10.0f) {
        return territoryGridFromPolitics(s.owner, s.rival, w, h, fc, cs, 0.0f, 0.0f);
    };
    CHECK_FALSE(territoryGridFromPolitics({}, {}, 0, 0, 2, 10.0f, 0.0f, 0.0f));
    CHECK_FALSE(build(11, 1, 2));
    CHECK_FALSE(build(12, 1, 0));
    CHECK_FALSE(build(12, 1, static_cast<int>(kTerritoryAudioMaxFactions) + 1));
    CHECK_FALSE(build(12, 1, 2, 0.0f));
    CHECK_FALSE(build(12, 1, 2, std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(build(12, 1, 2, std::numeric_limits<float>::infinity()));
    CHECK_FALSE(build(0xFFFFFFFFu, 0xFFFFFFFFu, 2));
    CHECK_FALSE(build(1u << 20, 1u << 20, 2));
    CHECK_FALSE(territoryGridFromPolitics(s.owner, s.rival, 12, 1, 2, 10.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f));

    Strip bad = s;
    bad.owner[3] = 2;
    CHECK_FALSE(bad.grid());
    bad = s;
    bad.rival[3] = 7;
    CHECK_FALSE(bad.grid());
    bad = s;
    bad.owner[3] = N;
    bad.rival[3] = 1;
    CHECK_FALSE(bad.grid());
    bad = s;
    bad.rival[3] = 0;
    CHECK_FALSE(bad.grid());

    TerritoryPoliticsParams p;
    p.bleedCells = 5;
    CHECK_FALSE(s.grid(p));
    p.bleedCells = -1;
    CHECK_FALSE(s.grid(p));
    p = {};
    p.ownerStrength = 1001;
    CHECK_FALSE(s.grid(p));
    p = {};
    p.contestedRivalStrength = 5000;
    CHECK_FALSE(s.grid(p));
}

TEST_CASE("hostile listener positions never give a loud or invalid result", "[territory_politics]")
{
    const Strip s;
    const auto g = s.grid();
    REQUIRE(g);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const float x : {nan, std::numeric_limits<float>::infinity(), -1e30f, 1e30f}) {
        const TerritoryAudioMix m = territoryAudioMix(*g, layers(), x, x);
        for (const float v : m.gains) {
            CHECK(v >= 0.0f);
            CHECK(v <= 1.0f);
        }
    }
}

TEST_CASE("the tracker holds the dominant layer across jitter on a border", "[territory_politics]")
{
    const Strip s;
    const auto g = s.grid();
    REQUIRE(g);

    const auto flips = [&](bool useTracker) {
        TerritoryTracker t;
        int changes = 0;
        int last = -2;
        for (int i = 0; i < 400; ++i) {
            const float x = 60.0f + ((i % 2 == 0) ? -3.0f : 3.0f);
            const TerritoryAudioMix m = mixAt(*g, x);
            const int dom = useTracker ? territoryTrack(t, m).dominant : m.dominant;
            if (dom != last) {
                ++changes;
            }
            last = dom;
        }
        return changes;
    };
    CHECK(flips(false) > 100);
    CHECK(flips(true) <= 2);
}

TEST_CASE("the tracker still follows a real crossing", "[territory_politics]")
{
    const Strip s;
    const auto g = s.grid();
    REQUIRE(g);
    TerritoryTracker t;
    int changes = 0;
    for (float x = 5.0f; x <= 115.0f; x += 0.5f) {
        if (territoryTrack(t, mixAt(*g, x)).changed) {
            ++changes;
        }
    }
    CHECK(t.held == 1);
    CHECK(changes == 2);
}

TEST_CASE("the tracker thresholds", "[territory_politics]")
{
    TerritoryTracker t;
    CHECK(territoryTrack(t, fake(0.1f, 0.0f)).dominant == -1);
    CHECK(territoryTrack(t, fake(0.25f, 0.0f)).dominant == 0);
    CHECK(territoryTrack(t, fake(0.1f, 0.0f)).dominant == 0);
    CHECK(territoryTrack(t, fake(0.04f, 0.0f)).dominant == -1);
    t = {};
    territoryTrack(t, fake(0.5f, 0.0f));
    CHECK(territoryTrack(t, fake(0.4f, 0.5f)).dominant == 0);
    CHECK(territoryTrack(t, fake(0.3f, 0.5f)).dominant == 1);
}

TEST_CASE("the tracker survives hostile state", "[territory_politics]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    TerritoryTracker t;
    t.held = 99;
    CHECK(territoryTrack(t, fake(0.5f, 0.0f)).dominant == 0);
    t.held = -50;
    CHECK(territoryTrack(t, fake(0.5f, 0.0f)).dominant == 0);
    CHECK(territoryTrack(t, fake(nan, nan)).dominant == -1);
    CHECK(territoryTrack(t, TerritoryAudioMix{}).dominant == -1);
    TerritoryHysteresis h;
    h.switchMargin = nan;
    h.releaseBelow = 5.0f;
    h.acquireAbove = -3.0f;
    TerritoryTracker u;
    const TerritoryTrackResult r = territoryTrack(u, fake(0.5f, 0.0f), h);
    CHECK(r.dominant >= -1);
    CHECK(r.dominant <= 1);
}
