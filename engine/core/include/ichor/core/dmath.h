#pragma once

#include <cmath>

namespace ichor {

// Sine and cosine built from additions, multiplications and one correctly
// rounded floor, so every IEEE 754 platform agrees bit for bit. The libm
// versions do not, and the state hash walks rotations produced by these.
// Accuracy is about 1e-7 over the full range, which is float precision.
namespace dmath {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = 1.57079632679489661923f;
constexpr float kQuarterPi = 0.78539816339744830962f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kInvTwoPi = 0.15915494309189533577f;

namespace detail {

// Cephes sinf and cosf kernels, valid on [0, pi/4].
inline float sinKernel(float y)
{
    const float z = y * y;
    float p = -1.9515295891e-4f;
    p = p * z + 8.3321608736e-3f;
    p = p * z - 1.6666654611e-1f;
    return y + y * z * p;
}

inline float cosKernel(float y)
{
    const float z = y * y;
    float p = 2.443315711809948e-5f;
    p = p * z - 1.388731625493765e-3f;
    p = p * z + 4.166664568298827e-2f;
    return 1.0f - 0.5f * z + z * z * p;
}

// Folds x into y in [0, pi/2] with the sign of sin(x) and cos(x).
struct Fold {
    float y;
    float sinSign;
    float cosSign;
};

inline Fold fold(float x)
{
    const float turns = std::floor(x * kInvTwoPi + 0.5f);
    x = x - turns * kTwoPi; // [-pi, pi]
    Fold f{x < 0.0f ? -x : x, x < 0.0f ? -1.0f : 1.0f, 1.0f};
    if (f.y > kHalfPi) {
        f.y = kPi - f.y;
        f.cosSign = -1.0f;
    }
    return f;
}

}

inline float sin(float x)
{
    const detail::Fold f = detail::fold(x);
    const float v = f.y <= kQuarterPi ? detail::sinKernel(f.y) : detail::cosKernel(kHalfPi - f.y);
    return f.sinSign * v;
}

inline float cos(float x)
{
    const detail::Fold f = detail::fold(x);
    const float v = f.y <= kQuarterPi ? detail::cosKernel(f.y) : detail::sinKernel(kHalfPi - f.y);
    return f.cosSign * v;
}

}

}
