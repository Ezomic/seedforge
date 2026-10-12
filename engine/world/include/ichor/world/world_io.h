#pragma once

#include "ichor/world/heightfield.h"

#include <filesystem>

namespace ichor {

// The forged world on disk, as doc 14 lays it out: a manifest beside the
// raw 16-bit heightmap, written once and never regenerated for a campaign.
bool writeWorld(const std::filesystem::path& directory, const Heightfield& heightfield);
bool readWorld(const std::filesystem::path& directory, Heightfield& heightfield);

}
