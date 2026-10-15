#pragma once

#include <cstddef>
#include <span>

namespace ichor {

// Doc 01 "Temenos": power exists only on consecrated ground. Each sanctuary lights a disc of the
// world, brightest at its centre and fading to nothing at its rim; the field is the sum of those
// discs. Plain float arithmetic with no libm calls, so the simulation can read it and every
// machine agrees bit for bit.
struct TemenosSanctuary {
    float x = 0.0f;
    float z = 0.0f;
    float radius = 0.0f;   // metres; the field is zero at and beyond this distance
    float strength = 0.0f; // field value at the centre, 0 to 1
};

constexpr float kTemenosFieldMax = 1.0f;
// Only the first this-many sanctuaries are read, so a hostile list cannot make a query unbounded.
constexpr std::size_t kMaxTemenosSanctuaries = 4096;

// The field at a point, 0 to kTemenosFieldMax. Sanctuaries add in list order; one with a
// non-finite position, a radius that is not positive and finite, or a strength outside 0..1 is
// skipped. A query point that is not finite reads as unconsecrated ground.
float temenosField(std::span<const TemenosSanctuary> sanctuaries, float x, float z);

// How much of a power's strength is available at a field value: floor off consecrated ground,
// rising linearly to full strength at kTemenosFieldMax. Both inputs are clamped to 0..1, and a
// value that is not a number counts as 0.
float temenosPower(float field, float offGroundFloor);

}
