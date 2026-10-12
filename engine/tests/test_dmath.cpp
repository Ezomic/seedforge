#include "ichor/core/dmath.h"

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
