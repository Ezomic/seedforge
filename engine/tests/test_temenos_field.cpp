#include "ichor/world/temenos_field.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace ichor;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kMax = std::numeric_limits<float>::max();

TemenosSanctuary grove(float x, float z, float radius = 100.0f, float strength = 1.0f)
{
    return TemenosSanctuary{x, z, radius, strength};
}

}

TEST_CASE("the field peaks at a sanctuary and is zero outside its reach", "[temenos]")
{
    const std::vector<TemenosSanctuary> s{grove(10.0f, 20.0f, 50.0f, 0.8f)};
    CHECK(temenosField(s, 10.0f, 20.0f) == 0.8f);
    CHECK(temenosField(s, 60.0f, 20.0f) == 0.0f);
    CHECK(temenosField(s, 1000.0f, 1000.0f) == 0.0f);
    CHECK(temenosField({}, 0.0f, 0.0f) == 0.0f);
}

TEST_CASE("the field falls monotonically with distance, with no seam at the edge", "[temenos]")
{
    const std::vector<TemenosSanctuary> s{grove(0.0f, 0.0f, 100.0f)};
    float prev = temenosField(s, 0.0f, 0.0f);
    for (int d = 1; d <= 100; ++d) {
        const float v = temenosField(s, static_cast<float>(d), 0.0f);
        CHECK(v < prev);
        prev = v;
    }
    CHECK(temenosField(s, 99.9f, 0.0f) < 1e-3f);
    CHECK(temenosField(s, 50.0f, 0.0f) > 0.3f);
}

TEST_CASE("the field is symmetric around a sanctuary", "[temenos]")
{
    const std::vector<TemenosSanctuary> s{grove(5.0f, 5.0f, 40.0f)};
    CHECK(temenosField(s, 15.0f, 5.0f) == temenosField(s, -5.0f, 5.0f));
    CHECK(temenosField(s, 5.0f, 15.0f) == temenosField(s, 5.0f, -5.0f));
}

TEST_CASE("overlapping sanctuaries add and the sum is capped at one", "[temenos]")
{
    const std::vector<TemenosSanctuary> one{grove(0.0f, 0.0f, 100.0f, 0.4f)};
    const std::vector<TemenosSanctuary> two{grove(0.0f, 0.0f, 100.0f, 0.4f), grove(0.0f, 0.0f, 100.0f, 0.4f)};
    CHECK(temenosField(two, 30.0f, 0.0f) > temenosField(one, 30.0f, 0.0f));
    const std::vector<TemenosSanctuary> many(10, grove(0.0f, 0.0f, 100.0f, 1.0f));
    CHECK(temenosField(many, 0.0f, 0.0f) == kTemenosFieldMax);
}

TEST_CASE("the same inputs give bit-identical results", "[temenos]")
{
    const std::vector<TemenosSanctuary> s{grove(3.0f, 4.0f, 77.0f, 0.6f), grove(-20.0f, 9.0f, 33.0f, 0.9f)};
    for (float x = -60.0f; x < 60.0f; x += 7.3f) {
        CHECK(temenosField(s, x, 1.5f) == temenosField(s, x, 1.5f));
    }
}

TEST_CASE("a query that is not a number or not finite is off consecrated ground", "[temenos]")
{
    const std::vector<TemenosSanctuary> s{grove(0.0f, 0.0f)};
    CHECK(temenosField(s, kNaN, 0.0f) == 0.0f);
    CHECK(temenosField(s, 0.0f, kNaN) == 0.0f);
    CHECK(temenosField(s, kInf, 0.0f) == 0.0f);
    CHECK(temenosField(s, 0.0f, -kInf) == 0.0f);
}

TEST_CASE("extreme coordinates never overflow into a nonsense value", "[temenos]")
{
    const std::vector<TemenosSanctuary> s{grove(0.0f, 0.0f), grove(kMax, kMax), grove(-kMax, 0.0f)};
    CHECK(temenosField(s, kMax, -kMax) == 0.0f);
    CHECK(temenosField(s, -kMax, kMax) == 0.0f);
    CHECK(temenosField(s, 1e30f, 1e30f) == 0.0f);
    CHECK(temenosField(s, kMax, kMax) == 1.0f);
}

TEST_CASE("a malformed sanctuary is ignored, the rest still count", "[temenos]")
{
    const std::vector<TemenosSanctuary> bad{
        grove(kNaN, 0.0f), grove(0.0f, kInf), grove(0.0f, 0.0f, kNaN), grove(0.0f, 0.0f, 0.0f),
        grove(0.0f, 0.0f, -5.0f), grove(0.0f, 0.0f, kInf), grove(0.0f, 0.0f, 100.0f, kNaN),
        grove(0.0f, 0.0f, 100.0f, -1.0f), grove(0.0f, 0.0f, 100.0f, kInf),
    };
    CHECK(temenosField(bad, 0.0f, 0.0f) == 0.0f);
    std::vector<TemenosSanctuary> mixed = bad;
    mixed.push_back(grove(0.0f, 0.0f, 100.0f, 0.5f));
    CHECK(temenosField(mixed, 0.0f, 0.0f) == 0.5f);
}

TEST_CASE("a radius whose square leaves the float range is skipped", "[temenos]")
{
    const std::vector<TemenosSanctuary> s{grove(0.0f, 0.0f, 1e30f), grove(0.0f, 0.0f, 1e-30f)};
    CHECK(temenosField(s, 1e10f, 0.0f) == 0.0f);
    CHECK(temenosField(s, 0.0f, 0.0f) == 0.0f);
}

TEST_CASE("the field matches exact reference values", "[temenos]")
{
    const std::vector<TemenosSanctuary> s{grove(0.0f, 0.0f, 100.0f, 1.0f)};
    CHECK(temenosField(s, 50.0f, 0.0f) == 0.5625f);
    CHECK(temenosField(s, 0.0f, 60.0f) == Catch::Approx(0.4096f).margin(1e-6));
    CHECK(temenosPower(1.0f, 0.1f) == 1.0f);
    CHECK(temenosPower(1.0f, 0.3f) == 1.0f);
}

TEST_CASE("a sanctuary list beyond the cap is clamped, not walked in full", "[temenos]")
{
    std::vector<TemenosSanctuary> s(kMaxTemenosSanctuaries, grove(1e6f, 1e6f, 1.0f, 1.0f));
    s.push_back(grove(0.0f, 0.0f, 100.0f, 1.0f));
    CHECK(temenosField(s, 0.0f, 0.0f) == 0.0f);
}

TEST_CASE("positional power runs from the off-ground floor to full strength", "[temenos]")
{
    CHECK(temenosPower(0.0f, 0.25f) == 0.25f);
    CHECK(temenosPower(1.0f, 0.25f) == 1.0f);
    CHECK(temenosPower(0.5f, 0.0f) == 0.5f);
    CHECK(temenosPower(0.0f, 0.0f) == 0.0f);
    CHECK(temenosPower(0.5f, 0.5f) > temenosPower(0.25f, 0.5f));
}

TEST_CASE("positional power clamps and rejects bad input", "[temenos]")
{
    CHECK(temenosPower(5.0f, 0.25f) == 1.0f);
    CHECK(temenosPower(-5.0f, 0.25f) == 0.25f);
    CHECK(temenosPower(kNaN, 0.25f) == 0.25f);
    CHECK(temenosPower(kInf, 0.25f) == 1.0f);
    CHECK(temenosPower(0.5f, kNaN) == 0.5f);
    CHECK(temenosPower(0.0f, 7.0f) == 1.0f);
    CHECK(temenosPower(0.0f, -7.0f) == 0.0f);
    CHECK(temenosPower(0.0f, -kInf) == 0.0f);
}
