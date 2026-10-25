#pragma once

#include "ichor/assets/bundle.h"

#include <cstdint>

namespace ichor {

// The dirt the forged road ribbons and settlement streets are drawn with: a square, mipmapped sRGB tile that is
// u across the road (0 and 1 are its edges) and v along it (it repeats). Two wheel ruts, a worn middle with grass
// tufts, pebbles, and edges that wear out into the ground's own green-brown, since the ribbon has no alpha to
// fade with. It depends on nothing but `size`: the same bytes on every machine.
Texture dirtRoadTexture(std::uint32_t size = 128);

}
