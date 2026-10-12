#include "ichor/core/dmath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ichor;

TEST_CASE("deterministic sine and cosine track libm within float precision")
{
    float worst = 0.0f;
    for (int i = -20000; i <= 20000; ++i) {
        const float x = static_cast<float>(i) * 0.001f;
        worst = std::max(worst, std::abs(dmath::sin(x) - std::sin(x)));
        worst = std::max(worst, std::abs(dmath::cos(x) - std::cos(x)));
    }
    REQUIRE(worst < 2e-6f);
    REQUIRE(dmath::sin(0.0f) == 0.0f);
    REQUIRE(dmath::cos(0.0f) == 1.0f);
}

TEST_CASE("deterministic atan2 tracks libm in every quadrant")
{
    float worst = 0.0f;
    for (int i = -40; i <= 40; ++i) {
        for (int j = -40; j <= 40; ++j) {
            const float x = static_cast<float>(i) * 0.25f;
            const float y = static_cast<float>(j) * 0.25f;
            if (x == 0.0f && y == 0.0f) continue;
            worst = std::max(worst, std::abs(dmath::atan2(y, x) - std::atan2(y, x)));
        }
    }
    REQUIRE(worst < 5e-6f);
    REQUIRE(dmath::atan2(0.0f, 0.0f) == 0.0f);
    REQUIRE(dmath::atan2(1.0f, 0.0f) == Catch::Approx(1.5707963f));
}
