#pragma once

#include <filesystem>
#include <optional>

namespace ichor {

// Where a packaged build keeps its files, all beside the executable: content/ (cooked bundles and fonts),
// shaders/, scripts/, data/ and art/. A checkout has none of these beside its executable, so the
// compiled-in build-tree paths apply there.
struct AssetRoots {
    std::filesystem::path content;
    std::filesystem::path shaders;
    std::filesystem::path scripts;
    std::filesystem::path data;
    std::filesystem::path art;
};

// The roots under exeDir when content/, shaders/ and data/ are all directories there; none otherwise.
std::optional<AssetRoots> packagedAssetRoots(const std::filesystem::path& exeDir);

// The folder holding the running executable; empty when the platform cannot say.
std::filesystem::path executableDirectory();

}
