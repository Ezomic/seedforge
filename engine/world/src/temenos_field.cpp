#include "ichor/world/temenos_field.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ichor {

namespace {

float clamp01(float v)
{
    if (!(v > 0.0f)) return 0.0f;
    return v < 1.0f ? v : 1.0f;
}

bool usable(const TemenosSanctuary& s)
{
    if (!std::isfinite(s.x) || !std::isfinite(s.z) || !std::isfinite(s.radius) || s.radius <= 0.0f) return false;
    if (!(s.strength >= 0.0f && s.strength <= 1.0f)) return false;
    // The squared radius must stay a normal float, or the falloff flattens or divides by zero.
    const float r2 = s.radius * s.radius;
    return std::isfinite(r2) && r2 >= std::numeric_limits<float>::min();
}

}

float temenosField(std::span<const TemenosSanctuary> sanctuaries, float x, float z)
{
    if (!std::isfinite(x) || !std::isfinite(z)) return 0.0f;
    const std::size_t count = std::min(sanctuaries.size(), kMaxTemenosSanctuaries);
    float sum = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const TemenosSanctuary& s = sanctuaries[i];
        if (!usable(s)) continue;
        // A difference or square that overflows to infinity puts the point far outside any rim.
        const float dx = x - s.x;
        const float dz = z - s.z;
        const float d2 = dx * dx + dz * dz;
        const float r2 = s.radius * s.radius;
        if (!(d2 < r2)) continue;
        const float t = 1.0f - d2 / r2;
        sum += s.strength * t * t;
    }
    return std::min(sum, kTemenosFieldMax);
}

float temenosPower(float field, float offGroundFloor)
{
    const float base = clamp01(offGroundFloor);
    const float f = clamp01(field);
    return base * (1.0f - f) + f;
}

}
