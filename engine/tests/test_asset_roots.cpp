#include "ichor/core/asset_roots.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace ichor;

namespace {

struct TempTree {
    std::filesystem::path root = test::tempPath("asset_roots_tree");
    TempTree() { std::filesystem::remove_all(root); std::filesystem::create_directories(root); }
    ~TempTree() { std::error_code ec; std::filesystem::remove_all(root, ec); }
};

}

TEST_CASE("a folder with content, shaders and data beside the exe is a packaged build")
{
    TempTree tree;
    for (const char* dir : {"content", "shaders", "data", "scripts", "art"}) {
        std::filesystem::create_directories(tree.root / dir);
    }
    const std::optional<AssetRoots> roots = packagedAssetRoots(tree.root);
    REQUIRE(roots.has_value());
    REQUIRE(roots->content == tree.root / "content");
    REQUIRE(roots->shaders == tree.root / "shaders");
    REQUIRE(roots->scripts == tree.root / "scripts");
    REQUIRE(roots->data == tree.root / "data");
    REQUIRE(roots->art == tree.root / "art");
}

TEST_CASE("a folder missing any required directory, or an empty or absent path, is not packaged")
{
    TempTree tree;
    REQUIRE_FALSE(packagedAssetRoots(tree.root).has_value());
    REQUIRE_FALSE(packagedAssetRoots({}).has_value());
    REQUIRE_FALSE(packagedAssetRoots(tree.root / "missing").has_value());
    std::filesystem::create_directories(tree.root / "content");
    std::filesystem::create_directories(tree.root / "shaders");
    REQUIRE_FALSE(packagedAssetRoots(tree.root).has_value());
}

TEST_CASE("a file named like a required directory does not make a packaged build")
{
    TempTree tree;
    std::filesystem::create_directories(tree.root / "content");
    std::filesystem::create_directories(tree.root / "shaders");
    std::ofstream(tree.root / "data").put('x');
    REQUIRE_FALSE(packagedAssetRoots(tree.root).has_value());
}

TEST_CASE("the test executable reports its own folder")
{
    const std::filesystem::path dir = executableDirectory();
    REQUIRE_FALSE(dir.empty());
    REQUIRE(std::filesystem::is_directory(dir));
}
