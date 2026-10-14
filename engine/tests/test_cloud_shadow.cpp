#include "ichor/gfx/cloud_shadow.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;
using Catch::Approx;

namespace {

const glm::vec3 kNoonSun{0.2f, 0.9f, 0.1f};
const glm::vec2 kWind{1.0f, 0.0f};

bool finite(const CloudShadow& s)
{
    return std::isfinite(s.strength) && std::isfinite(s.offset.x) && std::isfinite(s.offset.y);
}

}

TEST_CASE("a clear sky casts no cloud shadow")
{
    REQUIRE(cloudShadowFor(kNoonSun, 0.0f, {0.0, 50.0, 0.0}, kWind, 10.0f).strength == 0.0f);
    REQUIRE(cloudShadowFor(kNoonSun, 0.05f, {0.0, 50.0, 0.0}, kWind, 10.0f).strength == 0.0f);
}

TEST_CASE("cloud shadows grow with cover and never go past the floor")
{
    const float partial = cloudShadowFor(kNoonSun, 0.3f, {}, kWind, 0.0f).strength;
    const float broken = cloudShadowFor(kNoonSun, 0.6f, {}, kWind, 0.0f).strength;
    REQUIRE(partial > 0.0f);
    REQUIRE(broken > partial);
    for (float cover = 0.0f; cover <= 1.0f; cover += 0.05f) {
        const float strength = cloudShadowFor(kNoonSun, cover, {}, kWind, 0.0f).strength;
        REQUIRE(strength <= 1.0f - kCloudShadowFloor + 1e-6f);
        REQUIRE(strength >= 0.0f);
    }
}

TEST_CASE("a solid overcast has a milder pattern than broken cloud")
{
    REQUIRE(cloudShadowFor(kNoonSun, 1.0f, {}, kWind, 0.0f).strength < cloudShadowFor(kNoonSun, 0.75f, {}, kWind, 0.0f).strength);
}

TEST_CASE("cloud shadows fade out as the sun sinks and vanish below the horizon")
{
    const float high = cloudShadowFor(glm::vec3(0.0f, 0.9f, 0.4f), 0.7f, {}, kWind, 0.0f).strength;
    const float low = cloudShadowFor(glm::vec3(0.0f, 0.12f, 1.0f), 0.7f, {}, kWind, 0.0f).strength;
    REQUIRE(high > low);
    REQUIRE(low > 0.0f);
    REQUIRE(cloudShadowFor(glm::vec3(0.0f, 0.0f, 1.0f), 0.7f, {}, kWind, 0.0f).strength == 0.0f);
    REQUIRE(cloudShadowFor(glm::vec3(0.0f, -0.5f, 1.0f), 0.7f, {}, kWind, 0.0f).strength == 0.0f);
}

TEST_CASE("degenerate inputs give a finite, harmless shadow")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(cloudShadowFor(glm::vec3(0.0f), 0.7f, {}, kWind, 0.0f).strength == 0.0f);
    REQUIRE(finite(cloudShadowFor(kNoonSun, 0.7f, {nan, 0.0, nan}, kWind, 0.0f)));
    REQUIRE(finite(cloudShadowFor(kNoonSun, std::numeric_limits<float>::quiet_NaN(), {}, kWind, 0.0f)));
    REQUIRE(finite(cloudShadowFor(kNoonSun, 0.7f, {}, kWind, std::numeric_limits<float>::infinity())));
    REQUIRE(finite(cloudShadowFor(kNoonSun, 0.7f, {}, kWind, 1e30f)));
}

TEST_CASE("the shadow drifts with the wind and wraps instead of growing without bound")
{
    const CloudShadow start = cloudShadowFor(kNoonSun, 0.7f, {}, kWind, 0.0f);
    const CloudShadow later = cloudShadowFor(kNoonSun, 0.7f, {}, kWind, 60.0f);
    REQUIRE(later.offset.x > start.offset.x);
    REQUIRE(later.offset.y == Approx(start.offset.y));
    const CloudShadow huge = cloudShadowFor(kNoonSun, 0.7f, {}, kWind, 3.0e9f);
    REQUIRE(finite(huge));
    REQUIRE(std::abs(huge.offset.x) < 200.0f);
    const CloudShadow wrapped = cloudShadowFor(kNoonSun, 0.7f, {}, kWind, kCloudTimeWrap + 60.0f);
    REQUIRE(wrapped.offset.x == Approx(later.offset.x).margin(1e-3));
}

TEST_CASE("moving the camera shifts the pattern by the same ground distance, far from the origin too")
{
    const CloudShadow a = cloudShadowFor(kNoonSun, 0.7f, {0.0, 0.0, 0.0}, kWind, 0.0f);
    const CloudShadow b = cloudShadowFor(kNoonSun, 0.7f, {1200.0, 0.0, 600.0}, kWind, 0.0f);
    REQUIRE(b.offset.x - a.offset.x == Approx(1200.0f * kCloudPlanePerMetre).margin(1e-4));
    REQUIRE(b.offset.y - a.offset.y == Approx(600.0f * kCloudPlanePerMetre).margin(1e-4));

    // A metre of camera travel a million metres out still moves the pattern by a metre's worth.
    const CloudShadow far1 = cloudShadowFor(kNoonSun, 0.7f, {1.0e6, 0.0, 0.0}, kWind, 0.0f);
    const CloudShadow far2 = cloudShadowFor(kNoonSun, 0.7f, {1.0e6 + 1000.0, 0.0, 0.0}, kWind, 0.0f);
    REQUIRE(far2.offset.x - far1.offset.x == Approx(1000.0f * kCloudPlanePerMetre).epsilon(0.02));
}
