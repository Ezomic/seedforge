#pragma once

#include "ichor/assets/bundle.h"

#include <filesystem>

namespace ichor {

bool writeBundle(const std::filesystem::path& path, const SceneBundle& bundle);
bool readBundle(const std::filesystem::path& path, SceneBundle& bundle);

}
