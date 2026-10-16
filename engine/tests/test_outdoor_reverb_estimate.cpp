#include "ichor/audio/outdoor_reverb_estimate.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;

namespace {

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kEar = 1.7f;

OutdoorHeightFn plain()
{
    return [](float, float) { return 0.0f; };
}

// Two walls of the given height at |x| >= halfWidth, open along z.
OutdoorHeightFn canyon(float halfWidth, float wallHeight)
{
    return [=](float x, float) { return std::fabs(x) >= halfWidth ? wallHeight : 0.0f; };
}

// One wall at x >= distance.
OutdoorHeightFn cliff(float distance, float wallHeight)
{
    return [=](float x, float) { return x >= distance ? wallHeight : 0.0f; };
}

}  // namespace

TEST_CASE("a flat plain is fully open with no reverb", "[audio][outdoor_reverb]")
{
    const auto p = estimateOutdoorReverb(plain(), 0.0f, 0.0f, kEar);
    CHECK(p.openness == 1.0f);
    CHECK(p.wet == 0.0f);
    CHECK(p.nearestReflector == kOutdoorReverbRange);
    CHECK(p.decaySeconds == kOutdoorMinDecaySeconds);
}

TEST_CASE("a canyon sounds different from a plain", "[audio][outdoor_reverb]")
{
    const auto open = estimateOutdoorReverb(plain(), 0.0f, 0.0f, kEar);
    const auto c = estimateOutdoorReverb(canyon(15.0f, 40.0f), 0.0f, 0.0f, kEar);
    CHECK(c.openness < 0.5f);
    CHECK(c.wet > open.wet + 0.15f);
    CHECK(c.decaySeconds > open.decaySeconds + 0.5f);
    CHECK(c.nearestReflector < 20.0f);
    CHECK(c.earlyDelayMs < open.earlyDelayMs);
}

TEST_CASE("a single cliff wall reflects less than a canyon but more than a plain",
          "[audio][outdoor_reverb]")
{
    const auto open = estimateOutdoorReverb(plain(), 0.0f, 0.0f, kEar);
    const auto wall = estimateOutdoorReverb(cliff(15.0f, 40.0f), 0.0f, 0.0f, kEar);
    const auto c = estimateOutdoorReverb(canyon(15.0f, 40.0f), 0.0f, 0.0f, kEar);
    CHECK(wall.wet > open.wet);
    CHECK(c.wet > wall.wet);
    CHECK(wall.nearestReflector >= 14.0f);
    CHECK(wall.nearestReflector <= 20.0f);
    CHECK(wall.earlyDelayMs > 70.0f);
    CHECK(wall.earlyDelayMs < 110.0f);
}

TEST_CASE("terrain below ear level is not a reflector", "[audio][outdoor_reverb]")
{
    const auto p = estimateOutdoorReverb(canyon(15.0f, 0.5f), 0.0f, 0.0f, kEar);
    CHECK(p.wet == 0.0f);
    CHECK(p.openness == 1.0f);
}

TEST_CASE("a narrower canyon is wetter and the early echo comes sooner", "[audio][outdoor_reverb]")
{
    float lastWet = -1.0f;
    float lastDelay = 1.0e9f;
    for (const float half : {70.0f, 50.0f, 35.0f, 25.0f, 15.0f, 8.0f, 3.0f}) {
        const auto p = estimateOutdoorReverb(canyon(half, 40.0f), 0.0f, 0.0f, kEar);
        CHECK(p.wet >= lastWet);
        CHECK(p.earlyDelayMs <= lastDelay);
        lastWet = p.wet;
        lastDelay = p.earlyDelayMs;
    }
    CHECK(lastWet > 0.2f);
}

TEST_CASE("a taller wall never reduces the reverb", "[audio][outdoor_reverb]")
{
    float lastWet = -1.0f;
    for (const float h : {0.0f, 1.0f, 2.0f, 5.0f, 20.0f, 100.0f}) {
        const auto p = estimateOutdoorReverb(canyon(15.0f, h), 0.0f, 0.0f, kEar);
        CHECK(p.wet >= lastWet);
        lastWet = p.wet;
    }
}

TEST_CASE("outputs are deterministic and bounded", "[audio][outdoor_reverb]")
{
    const auto a = estimateOutdoorReverb(canyon(12.0f, 30.0f), 3.0f, -4.0f, kEar);
    const auto b = estimateOutdoorReverb(canyon(12.0f, 30.0f), 3.0f, -4.0f, kEar);
    CHECK(a.wet == b.wet);
    CHECK(a.decaySeconds == b.decaySeconds);
    CHECK(a.earlyDelayMs == b.earlyDelayMs);
    CHECK(a.wet <= kOutdoorMaxWet);
    CHECK(a.decaySeconds <= kOutdoorMaxDecaySeconds);
    CHECK(a.decaySeconds >= kOutdoorMinDecaySeconds);
}

TEST_CASE("the height function is called a bounded number of times", "[audio][outdoor_reverb]")
{
    int calls = 0;
    const OutdoorHeightFn counting = [&](float, float) {
        ++calls;
        return 100.0f;
    };
    estimateOutdoorReverb(counting, 0.0f, 0.0f, kEar);
    CHECK(calls <= kOutdoorReverbRays * kOutdoorReverbSteps);
    CHECK(calls > 0);
}

TEST_CASE("NaN and infinite heights are treated as open air", "[audio][outdoor_reverb]")
{
    for (const float bad : {kNan, kInf, -kInf}) {
        const auto p = estimateOutdoorReverb([=](float, float) { return bad; }, 0.0f, 0.0f, kEar);
        CHECK(p.wet == 0.0f);
        CHECK(p.openness == 1.0f);
    }
    const auto holes = estimateOutdoorReverb(
        [](float x, float) { return x > 10.0f ? kNan : (x < -15.0f ? 40.0f : 0.0f); }, 0.0f, 0.0f,
        kEar);
    CHECK(holes.wet > 0.0f);
    CHECK(std::isfinite(holes.wet));
    CHECK(std::isfinite(holes.earlyDelayMs));
}

TEST_CASE("extreme finite heights stay bounded", "[audio][outdoor_reverb]")
{
    const float big = std::numeric_limits<float>::max();
    for (const float h : {big, -big}) {
        const auto p = estimateOutdoorReverb(canyon(10.0f, h), 0.0f, 0.0f, kEar);
        CHECK(std::isfinite(p.wet));
        CHECK(p.wet >= 0.0f);
        CHECK(p.wet <= kOutdoorMaxWet);
        CHECK(p.decaySeconds <= kOutdoorMaxDecaySeconds);
    }
}

TEST_CASE("a hostile listener or missing function gives the open defaults",
          "[audio][outdoor_reverb]")
{
    const OutdoorReverbParams open;
    for (const float bad : {kNan, kInf, -kInf}) {
        const auto p1 = estimateOutdoorReverb(canyon(5.0f, 40.0f), bad, 0.0f, kEar);
        const auto p2 = estimateOutdoorReverb(canyon(5.0f, 40.0f), 0.0f, bad, kEar);
        const auto p3 = estimateOutdoorReverb(canyon(5.0f, 40.0f), 0.0f, 0.0f, bad);
        for (const auto& p : {p1, p2, p3}) {
            CHECK(p.wet == open.wet);
            CHECK(p.openness == open.openness);
            CHECK(p.earlyDelayMs == open.earlyDelayMs);
        }
    }
    const auto none = estimateOutdoorReverb(OutdoorHeightFn{}, 0.0f, 0.0f, kEar);
    CHECK(none.wet == 0.0f);
    const auto far = estimateOutdoorReverb(canyon(5.0f, 40.0f), 1.0e30f, 0.0f, kEar);
    CHECK(std::isfinite(far.wet));
}
