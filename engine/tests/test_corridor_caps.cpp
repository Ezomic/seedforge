#include "ichor/graph/corridor_caps.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor::graph::corridor;

namespace {

Params params(std::uint64_t seed = 1, float cap = 10.0f)
{
    Params p;
    p.seed = seed;
    p.maxStraight = cap;
    return p;
}

std::vector<Segment> straight(float length)
{
    return {Segment{length, 2.0f, Variation::None}};
}

std::size_t variations(const std::vector<Segment>& path)
{
    std::size_t n = 0;
    for (const Segment& s : path)
        n += s.variation != Variation::None ? 1 : 0;
    return n;
}

float totalLength(const std::vector<Segment>& path)
{
    float sum = 0.0f;
    for (const Segment& s : path)
        sum += s.length;
    return sum;
}

} // namespace

TEST_CASE("a corridor at exactly the cap is untouched", "[corridor]")
{
    const Result r = applyCaps(straight(10.0f), params());
    REQUIRE(r.error.empty());
    REQUIRE(r.segments.size() == 1);
    REQUIRE(r.segments[0].length == 10.0f);
    REQUIRE(variations(r.segments) == 0);
}

TEST_CASE("a corridor just over the cap gets one forced variation", "[corridor]")
{
    const Result r = applyCaps(straight(10.5f), params());
    REQUIRE(r.error.empty());
    REQUIRE(variations(r.segments) == 1);
    REQUIRE(longestStraightRun(r.segments) <= 10.0f);
    REQUIRE(totalLength(r.segments) == 10.5f);
}

TEST_CASE("a long corridor is capped everywhere and keeps its length", "[corridor]")
{
    const Result r = applyCaps(straight(95.0f), params());
    REQUIRE(r.error.empty());
    REQUIRE(longestStraightRun(r.segments) <= 10.0f);
    REQUIRE(variations(r.segments) == 9);
    REQUIRE(std::fabs(totalLength(r.segments) - 95.0f) < 1e-3f);
}

TEST_CASE("the cap is measured across consecutive straight pieces", "[corridor]")
{
    const std::vector<Segment> path{{6.0f, 2.0f, Variation::None}, {6.0f, 2.0f, Variation::None}};
    const Result r = applyCaps(path, params());
    REQUIRE(r.error.empty());
    REQUIRE(longestStraightRun(r.segments) <= 10.0f);
    REQUIRE(variations(r.segments) == 1);
}

TEST_CASE("an authored variation resets the run", "[corridor]")
{
    const std::vector<Segment> path{
        {8.0f, 2.0f, Variation::None}, {0.0f, 2.0f, Variation::Bend}, {8.0f, 2.0f, Variation::None}};
    const Result r = applyCaps(path, params());
    REQUIRE(r.error.empty());
    REQUIRE(r.segments.size() == 3);
    REQUIRE(variations(r.segments) == 1);
}

TEST_CASE("without the caps the run is long, so the check is real", "[corridor]")
{
    const std::vector<Segment> path = straight(95.0f);
    REQUIRE(longestStraightRun(path) == 95.0f);
    const Result r = applyCaps(path, params());
    REQUIRE(longestStraightRun(r.segments) < 0.2f * longestStraightRun(path));
}

TEST_CASE("forced width changes stay in range and actually change", "[corridor]")
{
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        const Result r = applyCaps(straight(200.0f), params(seed));
        REQUIRE(r.error.empty());
        float prev = 2.0f;
        for (const Segment& s : r.segments) {
            REQUIRE(s.width >= kMinWidth);
            REQUIRE(s.width <= kMaxWidth);
            if (s.variation == Variation::WidthChange)
                REQUIRE(s.width != prev);
            prev = s.width;
        }
    }
}

TEST_CASE("the seed picks the variations and every kind occurs", "[corridor]")
{
    bool seen[4] = {};
    for (std::uint64_t seed = 0; seed < 20; ++seed)
        for (const Segment& s : applyCaps(straight(300.0f), params(seed)).segments)
            seen[static_cast<int>(s.variation)] = true;
    for (bool b : seen)
        REQUIRE(b);

    const Result a = applyCaps(straight(300.0f), params(1));
    const Result b = applyCaps(straight(300.0f), params(2));
    bool differs = a.segments.size() != b.segments.size();
    for (std::size_t i = 0; !differs && i < a.segments.size(); ++i)
        differs = a.segments[i].variation != b.segments[i].variation;
    REQUIRE(differs);
}

TEST_CASE("repeated runs give identical output", "[corridor]")
{
    const std::vector<Segment> path{
        {37.0f, 2.0f, Variation::None}, {0.0f, 3.0f, Variation::Landmark}, {55.0f, 3.0f, Variation::None}};
    const Result first = applyCaps(path, params(99));
    REQUIRE(first.error.empty());
    for (int i = 0; i < 5; ++i) {
        const Result again = applyCaps(path, params(99));
        REQUIRE(again.error == first.error);
        REQUIRE(again.segments.size() == first.segments.size());
        for (std::size_t j = 0; j < first.segments.size(); ++j) {
            REQUIRE(again.segments[j].length == first.segments[j].length);
            REQUIRE(again.segments[j].width == first.segments[j].width);
            REQUIRE(again.segments[j].variation == first.segments[j].variation);
        }
    }
}

TEST_CASE("zero length pieces and an empty path are fine", "[corridor]")
{
    REQUIRE(applyCaps({}, params()).segments.empty());
    const Result r = applyCaps(straight(0.0f), params());
    REQUIRE(r.error.empty());
    REQUIRE(r.segments.empty());
}

TEST_CASE("huge lengths fail cleanly instead of growing", "[corridor]")
{
    for (float huge : {1e9f, 1e30f, std::numeric_limits<float>::max()}) {
        const Result r = applyCaps(straight(huge), params());
        REQUIRE_FALSE(r.error.empty());
        REQUIRE(r.segments.empty());
    }
}

TEST_CASE("output is bounded", "[corridor]")
{
    const Result r = applyCaps(straight(20000.0f), params(1, 10.0f));
    REQUIRE(r.error.empty());
    REQUIRE(r.segments.size() <= kMaxOutputSegments);
    const Result big = applyCaps(straight(1.0e6f), params(1, kMinCap));
    REQUIRE_FALSE(big.error.empty());
}

TEST_CASE("NaN, infinity and negatives are rejected", "[corridor]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float bad : {nan, inf, -inf, -1.0f}) {
        REQUIRE_FALSE(applyCaps(straight(bad), params()).error.empty());
        REQUIRE_FALSE(applyCaps({{5.0f, bad, Variation::None}}, params()).error.empty());
        REQUIRE_FALSE(applyCaps(straight(5.0f), params(1, bad)).error.empty());
    }
    REQUIRE_FALSE(applyCaps(straight(5.0f), params(1, 0.0f)).error.empty());
    REQUIRE_FALSE(applyCaps(straight(5.0f), params(1, kMinCap * 0.5f)).error.empty());
    REQUIRE_FALSE(applyCaps({{5.0f, 0.0f, Variation::None}}, params()).error.empty());
    REQUIRE_FALSE(applyCaps({{5.0f, 100.0f, Variation::None}}, params()).error.empty());
}

TEST_CASE("an oversized input path is rejected", "[corridor]")
{
    const std::vector<Segment> path(kMaxInputSegments + 1, Segment{1.0f, 2.0f, Variation::None});
    REQUIRE_FALSE(applyCaps(path, params()).error.empty());
}

TEST_CASE("an unknown variation value is rejected", "[corridor]")
{
    const Segment s{1.0f, 2.0f, static_cast<Variation>(200)};
    REQUIRE_FALSE(applyCaps({s}, params()).error.empty());
}

TEST_CASE("a run landing exactly on the cap emits no zero length piece", "[corridor]")
{
    const std::vector<Segment> path{{10.0f, 2.0f, Variation::None}, {5.0f, 2.0f, Variation::None}};
    const Result r = applyCaps(path, params());
    REQUIRE(r.error.empty());
    REQUIRE(variations(r.segments) == 1);
    for (const Segment& s : r.segments)
        REQUIRE((s.variation != Variation::None || s.length > 0.0f));
    REQUIRE(longestStraightRun(r.segments) <= 10.0f);
}

TEST_CASE("a marker with a length is rejected", "[corridor]")
{
    REQUIRE_FALSE(applyCaps({{3.0f, 2.0f, Variation::Bend}}, params()).error.empty());
}

TEST_CASE("a forced width change carries into the next straight piece", "[corridor]")
{
    const std::vector<Segment> path{{15.0f, 2.0f, Variation::None}, {15.0f, 2.0f, Variation::None}};
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        const Result r = applyCaps(path, params(seed));
        REQUIRE(r.error.empty());
        float width = 2.0f;
        for (const Segment& s : r.segments) {
            if (s.variation == Variation::None)
                REQUIRE(s.width == width);
            else
                width = s.width;
        }
    }
}
