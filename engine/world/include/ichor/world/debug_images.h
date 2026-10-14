#pragma once

#include "ichor/world/heightfield.h"
#include "ichor/world/hydrology.h"
#include "ichor/world/map_image.h"

#include <cstdint>

namespace ichor {

// The most pixels a side of a debug picture has; a larger world is shrunk to fit, so one picture is
// at most 16 MiB however big the world.
constexpr std::uint32_t kDebugImageMaxSide = 2048;

// Pictures of the forge's intermediate fields, one pixel per sample (a block of samples once the
// world is wider than maxSide), north up like the map, opaque RGBA. They only read their inputs and
// the same inputs give the same bytes. A zero maxSide, an empty field, or a hydrology that is not the
// size of the heightfield gives an empty image.

// The sample heights as grey, the lowest sample black and the highest white (mid grey when all are level).
MapImage renderHeightImage(const Heightfield& heightfield, std::uint32_t maxSide = kDebugImageMaxSide);

// How many samples drain through each sample, on a log scale from black (one) to the biggest river
// in white. A block of samples shows its biggest.
MapImage renderFlowImage(const Hydrology& hydrology, std::uint32_t maxSide = kDebugImageMaxSide);

// The height in dark grey with rivers in blue and lakes in a deeper blue over it. A block of samples
// is a lake if any of it is, else a river if any of it is.
MapImage renderWaterImage(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& params, std::uint32_t maxSide = kDebugImageMaxSide);

}
