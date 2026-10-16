#include "ichor/audio/territory_audio_mix.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

// 8x1 strip, cell size 10. Faction 0 owns cells 0..3, faction 1 owns cells 4..7.
TerritoryAudioGrid strip()
{
    TerritoryAudioGrid g;
    g.width = 8;
    g.height = 1;
    g.cellSize = 10.0f;
    g.strength.assign(2, std::vector<std::uint16_t>(8, 0));
    for (int i = 0; i < 4; ++i) {
        g.strength[0][i] = 1000;
        g.strength[1][4 + i] = 1000;
    }
    return g;
}

std::vector<TerritoryAudioLayer> twoLayers()
{
    return {{"compact_bell", 1.0f}, {"garrison_horn", 0.5f}};
}

float sum(const std::vector<float>& v)
{
    float s = 0.0f;
    for (float x : v) {
        s += x;
    }
    return s;
}

}  // namespace

TEST_CASE("deep inside a territory only its layer plays, at its max gain", "[territory_audio]")
{
    const TerritoryAudioMix a = territoryAudioMix(strip(), twoLayers(), 5.0f, 5.0f);
    CHECK(a.gains[0] == Approx(1.0f));
    CHECK(a.gains[1] == Approx(0.0f));
    CHECK(a.dominant == 0);

    const TerritoryAudioMix b = territoryAudioMix(strip(), twoLayers(), 75.0f, 5.0f);
    CHECK(b.gains[0] == Approx(0.0f));
    CHECK(b.gains[1] == Approx(0.5f));
    CHECK(b.dominant == 1);
}

TEST_CASE("a border cross-fades: gains are continuous and the sum stays bounded", "[territory_audio]")
{
    const TerritoryAudioGrid g = strip();
    const std::vector<TerritoryAudioLayer> layers = {{"a", 1.0f}, {"b", 1.0f}};

    float maxJump = 0.0f;
    float maxSum = 0.0f;
    float prev0 = 1.0f;
    float meanMid = 0.0f;
    int midCount = 0;
    for (float x = 0.0f; x <= 80.0f; x += 0.05f) {
        const TerritoryAudioMix m = territoryAudioMix(g, layers, x, 5.0f);
        maxJump = std::max(maxJump, std::fabs(m.gains[0] - prev0));
        prev0 = m.gains[0];
        maxSum = std::max(maxSum, sum(m.gains));
        if (x > 35.0f && x < 45.0f) {
            meanMid += std::min(m.gains[0], m.gains[1]);
            ++midCount;
        }
    }
    // A hard nearest-cell switch would jump by 1.0 at the border; the cross-fade steps by 0.05 / 10.
    CHECK(maxJump < 0.01f);
    CHECK(maxSum <= 1.0001f);
    // Both layers are audible together around the border (a hard switch gives a mean of 0).
    CHECK(meanMid / static_cast<float>(midCount) > 0.1f);
}

TEST_CASE("the cross-fade is even at the border", "[territory_audio]")
{
    const std::vector<TerritoryAudioLayer> layers = {{"a", 1.0f}, {"b", 1.0f}};
    const TerritoryAudioMix m = territoryAudioMix(strip(), layers, 40.0f, 5.0f);
    CHECK(m.gains[0] == Approx(0.5f));
    CHECK(m.gains[1] == Approx(0.5f));
    CHECK(m.dominant == 0);
}

TEST_CASE("unclaimed land is silent and fades in at the edge of a claim", "[territory_audio]")
{
    TerritoryAudioGrid g;
    g.width = 4;
    g.height = 1;
    g.strength.assign(1, std::vector<std::uint16_t>{0, 0, 0, 1000});
    const std::vector<TerritoryAudioLayer> layers = {{"a", 1.0f}};

    const TerritoryAudioMix far = territoryAudioMix(g, layers, 0.5f, 0.5f);
    CHECK(far.gains[0] == 0.0f);
    CHECK(far.dominant == -1);

    float prev = 0.0f;
    float maxJump = 0.0f;
    for (float x = 0.0f; x <= 4.0f; x += 0.01f) {
        const float v = territoryAudioMix(g, layers, x, 0.5f).gains[0];
        maxJump = std::max(maxJump, std::fabs(v - prev));
        prev = v;
    }
    CHECK(maxJump < 0.02f);
    CHECK(prev == Approx(1.0f));
}

TEST_CASE("contested cells split loudness instead of adding it", "[territory_audio]")
{
    TerritoryAudioGrid g;
    g.width = 1;
    g.height = 1;
    g.strength = {{1000}, {1000}, {1000}};
    const std::vector<TerritoryAudioLayer> layers = {{"a", 1.0f}, {"b", 1.0f}, {"c", 1.0f}};
    const TerritoryAudioMix m = territoryAudioMix(g, layers, 0.5f, 0.5f);
    CHECK(sum(m.gains) <= 1.0001f);
    CHECK(m.gains[0] == Approx(1.0f / 3.0f));
    CHECK(m.dominant == 0);

    g.strength = {{1000}, {250}};
    const TerritoryAudioMix skew = territoryAudioMix(g, {{"a", 1.0f}, {"b", 1.0f}}, 0.5f, 0.5f);
    CHECK(skew.gains[0] > skew.gains[1]);
    CHECK(skew.gains[0] == Approx(0.8f));
}

TEST_CASE("a faction without a territory layer stays silent", "[territory_audio]")
{
    const std::vector<TerritoryAudioLayer> layers = {{"", 1.0f}, {"garrison_horn", 1.0f}};
    const TerritoryAudioMix m = territoryAudioMix(strip(), layers, 5.0f, 5.0f);
    CHECK(m.gains[0] == 0.0f);
    CHECK(m.gains[1] == 0.0f);
    CHECK(m.dominant == -1);
}

TEST_CASE("NaN, infinite and extreme positions never crash or get loud", "[territory_audio]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float big = std::numeric_limits<float>::max();
    const TerritoryAudioGrid g = strip();
    const auto layers = twoLayers();

    for (float bad : {nan, inf, -inf}) {
        const TerritoryAudioMix m = territoryAudioMix(g, layers, bad, 5.0f);
        CHECK(sum(m.gains) == 0.0f);
        CHECK(m.dominant == -1);
        CHECK(sum(territoryAudioMix(g, layers, 5.0f, bad).gains) == 0.0f);
    }
    for (float far : {big, -big, 1e30f, -1e30f}) {
        const TerritoryAudioMix m = territoryAudioMix(g, layers, far, far);
        CHECK(sum(m.gains) <= 1.0001f);
    }
    CHECK(territoryAudioMix(g, layers, -1e9f, 5.0f).gains[0] == Approx(1.0f));
    CHECK(territoryAudioMix(g, layers, 1e9f, 5.0f).gains[1] == Approx(0.5f));
}

TEST_CASE("malformed grids and layers give silence", "[territory_audio]")
{
    const auto layers = twoLayers();
    TerritoryAudioGrid g = strip();

    g.cellSize = 0.0f;
    CHECK(sum(territoryAudioMix(g, layers, 5.0f, 5.0f).gains) == 0.0f);
    g.cellSize = std::numeric_limits<float>::quiet_NaN();
    CHECK(sum(territoryAudioMix(g, layers, 5.0f, 5.0f).gains) == 0.0f);

    g = strip();
    g.width = 0;
    CHECK(sum(territoryAudioMix(g, layers, 5.0f, 5.0f).gains) == 0.0f);

    g = strip();
    g.width = 0xFFFFFFFFu;
    g.height = 0xFFFFFFFFu;
    CHECK(sum(territoryAudioMix(g, layers, 5.0f, 5.0f).gains) == 0.0f);

    g = strip();
    g.strength[1].resize(3);
    CHECK(territoryAudioMix(g, layers, 75.0f, 5.0f).gains[1] == 0.0f);
    CHECK(territoryAudioMix(g, layers, 5.0f, 5.0f).gains[0] == Approx(1.0f));

    g = strip();
    g.strength[0][0] = 60000;
    CHECK(territoryAudioMix(g, layers, 5.0f, 5.0f).gains[0] == Approx(1.0f));

    g = strip();
    CHECK(territoryAudioMix(g, {{"a", 7.0f}, {"b", -3.0f}}, 5.0f, 5.0f).gains[0] == Approx(1.0f));
    CHECK(territoryAudioMix(g, {{"a", std::numeric_limits<float>::quiet_NaN()}}, 5.0f, 5.0f).gains[0] == 0.0f);

    CHECK(territoryAudioMix(g, {}, 5.0f, 5.0f).gains.empty());
    g.strength.clear();
    CHECK(sum(territoryAudioMix(g, layers, 5.0f, 5.0f).gains) == 0.0f);
}

TEST_CASE("layers beyond the factions are silent, extra factions are ignored", "[territory_audio]")
{
    const TerritoryAudioMix m = territoryAudioMix(strip(), {{"a", 1.0f}, {"b", 1.0f}, {"c", 1.0f}}, 5.0f, 5.0f);
    REQUIRE(m.gains.size() == 3);
    CHECK(m.gains[2] == 0.0f);
    CHECK(territoryAudioMix(strip(), {{"a", 1.0f}}, 75.0f, 5.0f).gains.size() == 1);
}

TEST_CASE("the mix is deterministic", "[territory_audio]")
{
    const TerritoryAudioGrid g = strip();
    const auto layers = twoLayers();
    for (float x = -5.0f; x < 90.0f; x += 1.7f) {
        const TerritoryAudioMix a = territoryAudioMix(g, layers, x, 3.0f);
        const TerritoryAudioMix b = territoryAudioMix(g, layers, x, 3.0f);
        CHECK(a.gains == b.gains);
        CHECK(a.dominant == b.dominant);
    }
}

TEST_CASE("a position jump is slewed instead of popping", "[territory_audio]")
{
    const std::vector<float> from = {1.0f, 0.0f};
    const std::vector<float> to = {0.0f, 1.0f};

    std::vector<float> cur = from;
    float maxJump = 0.0f;
    for (int i = 0; i < 101; ++i) {
        const std::vector<float> next = territoryAudioStep(cur, to, 0.01f, 1.0f);
        maxJump = std::max({maxJump, std::fabs(next[0] - cur[0]), std::fabs(next[1] - cur[1])});
        cur = next;
    }
    CHECK(maxJump <= 0.0101f);
    CHECK(cur[0] == Approx(0.0f).margin(1e-4));
    CHECK(cur[1] == Approx(1.0f).margin(1e-4));
    CHECK(territoryAudioStep(from, to, 0.01f, 1.0f)[1] < 0.02f);
}

TEST_CASE("territoryAudioStep handles bad input", "[territory_audio]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(territoryAudioStep({0.5f}, {1.0f}, 0.1f, 0.0f)[0] == 1.0f);
    CHECK(territoryAudioStep({0.5f}, {1.0f}, 0.1f, nan)[0] == 1.0f);
    CHECK(territoryAudioStep({0.5f}, {1.0f}, nan, 1.0f)[0] == 0.5f);
    CHECK(territoryAudioStep({0.5f}, {1.0f}, -1.0f, 1.0f)[0] == 0.5f);
    CHECK(territoryAudioStep({}, {0.0f, 0.0f}, 0.1f, 1.0f).size() == 2);
    CHECK(territoryAudioStep({1.0f, 1.0f, 1.0f}, {0.0f}, 0.1f, 1.0f).size() == 1);
    CHECK(territoryAudioStep({nan}, {nan}, 0.1f, 1.0f)[0] == 0.0f);
    CHECK(territoryAudioStep({0.0f}, {1.0f}, std::numeric_limits<float>::infinity(), 1.0f)[0] == 0.0f);
}
