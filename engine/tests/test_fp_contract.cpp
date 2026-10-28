#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstdint>

namespace ichor::fp_probe {
float mul_add(float a, float b, float c);
double mul_add(double a, double b, double c);
}

// a * a is exactly 1 + 2e + e^2. Rounded on its own the e^2 term is lost and adding -(1 + 2e) gives +0;
// fused into one rounding it survives. So the bit pattern tells a separate multiply and add from an FMA.
TEST_CASE("a float multiply-add is rounded twice, not fused, where FMA instructions are available")
{
    const float a = 1.0f + 0x1p-13f;
    const float c = -(1.0f + 0x1p-12f);
    REQUIRE(std::fma(a, a, c) == 0x1p-26f);
    REQUIRE(std::bit_cast<std::uint32_t>(ichor::fp_probe::mul_add(a, a, c)) == 0u);
}

TEST_CASE("a double multiply-add is rounded twice, not fused, where FMA instructions are available")
{
    const double a = 1.0 + 0x1p-27;
    const double c = -(1.0 + 0x1p-26);
    REQUIRE(std::fma(a, a, c) == 0x1p-54);
    REQUIRE(std::bit_cast<std::uint64_t>(ichor::fp_probe::mul_add(a, a, c)) == 0u);
}
