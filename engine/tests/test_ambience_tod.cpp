#include "ichor/audio/ambience_tod.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;

namespace {

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

bool bounded(const AmbienceTimeOfDay& g)
{
    for (float v : {g.dawnBirds, g.dayInsects, g.dusk, g.night}) {
        if (!(v >= 0.0f && v <= 1.0f)) return false;
    }
    return ambienceTimeOfDayTotal(g) <= 1.0001f;
}

float maxStep(float from, float to, float step)
{
    float worst = 0.0f;
    AmbienceTimeOfDay prev = ambienceTimeOfDay(from);
    for (float h = from + step; h <= to; h += step) {
        const auto cur = ambienceTimeOfDay(h);
        for (auto [a, b] : {std::pair{prev.dawnBirds, cur.dawnBirds}, {prev.dayInsects, cur.dayInsects},
                            {prev.dusk, cur.dusk}, {prev.night, cur.night}}) {
            worst = std::max(worst, std::fabs(a - b));
        }
        prev = cur;
    }
    return worst;
}

}  // namespace

TEST_CASE("each time of day is dominated by its own layer", "[audio][tod]")
{
    CHECK(ambienceTimeOfDay(6.5f).dawnBirds == 1.0f);
    CHECK(ambienceTimeOfDay(13.0f).dayInsects == 1.0f);
    CHECK(ambienceTimeOfDay(19.5f).dusk == 1.0f);
    CHECK(ambienceTimeOfDay(0.0f).night == 1.0f);
    CHECK(ambienceTimeOfDay(13.0f).night == 0.0f);
}

TEST_CASE("pre-dawn is a deliberate near-silence", "[audio][tod]")
{
    CHECK(ambienceTimeOfDayTotal(ambienceTimeOfDay(4.75f)) == 0.0f);
    CHECK(ambienceTimeOfDayTotal(ambienceTimeOfDay(4.75f)) < ambienceTimeOfDayTotal(ambienceTimeOfDay(13.0f)));
}

TEST_CASE("crossfades are continuous across every boundary", "[audio][tod]")
{
    // A hard step would jump by ~1 between neighbouring samples; the smooth fade moves by far less.
    const float step = 1.0f / 600.0f;
    CHECK(maxStep(0.0f, 24.0f, step) < 0.02f);
    // Controls the metric: the fade really does traverse most of the range inside its window.
    CHECK(ambienceTimeOfDay(7.0f).dawnBirds == 1.0f);
    CHECK(ambienceTimeOfDay(9.0f).dawnBirds == 0.0f);
}

TEST_CASE("midnight wraps without a seam", "[audio][tod]")
{
    CHECK(maxStep(23.9f, 24.0f, 0.001f) < 0.01f);
    const auto a = ambienceTimeOfDay(23.9999f);
    const auto b = ambienceTimeOfDay(0.0001f);
    CHECK(std::fabs(a.night - b.night) < 0.001f);
    const auto end = ambienceTimeOfDay(24.0f);
    const auto start = ambienceTimeOfDay(0.0f);
    CHECK(end.night == start.night);
    CHECK(end.dusk == start.dusk);
    CHECK(ambienceTimeOfDay(-1.0f).night == ambienceTimeOfDay(23.0f).night);
    CHECK(ambienceTimeOfDay(49.0f).night == ambienceTimeOfDay(1.0f).night);
}

TEST_CASE("weights stay bounded everywhere", "[audio][tod]")
{
    for (float h = -50.0f; h < 75.0f; h += 0.01f) CHECK(bounded(ambienceTimeOfDay(h)));
}

TEST_CASE("hostile hours are bounded and deterministic", "[audio][tod]")
{
    for (float h : {kNan, kInf, -kInf, 1.0e30f, -1.0e30f, 3.4e38f, -3.4e38f, 1.0e6f, -1.0e6f, 999999.0f, -0.0f}) {
        const auto g = ambienceTimeOfDay(h);
        CHECK(bounded(g));
        const float w = ambienceWrapHour(h);
        CHECK((w >= 0.0f && w < 24.0f));
        CHECK(ambienceTimeOfDay(h).night == g.night);
    }
    CHECK(ambienceTimeOfDay(kNan).dayInsects == 1.0f);
    CHECK(bounded(ambienceTimeOfDay(-0.0000001f)));
}
