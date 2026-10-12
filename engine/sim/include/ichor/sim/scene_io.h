#pragma once

#include <filesystem>

namespace ichor {

class Scene;

// Snapshot of a scene: clock, per-system generators, every entity by StableId.
// Load restores into an existing scene (its seed must match) and clears what
// was there. The command log is not part of a snapshot; it is history.
bool saveScene(const std::filesystem::path& path, const Scene& scene);
bool loadScene(const std::filesystem::path& path, Scene& scene);

}
