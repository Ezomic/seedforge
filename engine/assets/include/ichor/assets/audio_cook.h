#pragma once

#include "ichor/assets/bundle.h"

#include <filesystem>

namespace ichor {

// Decodes a WAV (or anything miniaudio reads) to mono 16-bit at its native
// rate. The mixer resamples at play time.
bool cookSound(const std::filesystem::path& source, SoundClip& out);

}
