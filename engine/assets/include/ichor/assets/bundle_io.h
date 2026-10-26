#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/assets/piece_data.h"

#include <filesystem>

namespace ichor {

bool writeBundle(const std::filesystem::path& path, const SceneBundle& bundle);
bool readBundle(const std::filesystem::path& path, SceneBundle& bundle);

// The same with the sockets, LOD groups and collision shapes a bundle may carry after its nodes.
bool writeBundle(const std::filesystem::path& path, const SceneBundle& bundle, const PieceData& pieces);
bool readBundle(const std::filesystem::path& path, SceneBundle& bundle, PieceData& pieces);

}
