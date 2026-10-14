#include "ichor/core/file_io.h"
#include "ichor/world/world_io.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace ichor;

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& name)
    {
#ifdef _WIN32
        const auto pid = static_cast<unsigned long>(_getpid());
#else
        const auto pid = static_cast<unsigned long>(getpid());
#endif
        path = std::filesystem::temp_directory_path() / ("ichor-file-io-" + name + "-" + std::to_string(pid));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}

TEST_CASE("readFileBytes gives a regular file whole, and nothing for anything else")
{
    TempDir dir("read");
    const std::vector<std::uint8_t> content{1, 2, 3, 250, 0, 7};
    REQUIRE(writeFileAtomically(dir.path / "file.bin", content));

    std::vector<std::uint8_t> bytes{9, 9};
    REQUIRE(readFileBytes(dir.path / "file.bin", bytes));
    REQUIRE(bytes == content);

    SECTION("missing")
    {
        REQUIRE_FALSE(readFileBytes(dir.path / "absent.bin", bytes));
        REQUIRE(bytes.empty());
    }
    SECTION("a directory")
    {
        std::filesystem::create_directories(dir.path / "folder");
        REQUIRE_FALSE(readFileBytes(dir.path / "folder", bytes));
        REQUIRE(bytes.empty());
    }
    SECTION("longer than the most allowed")
    {
        REQUIRE_FALSE(readFileBytes(dir.path / "file.bin", bytes, content.size() - 1));
        REQUIRE(bytes.empty());
        REQUIRE(readFileBytes(dir.path / "file.bin", bytes, content.size()));
    }
    SECTION("empty")
    {
        REQUIRE(writeFileAtomically(dir.path / "empty.bin", {}));
        REQUIRE(readFileBytes(dir.path / "empty.bin", bytes));
        REQUIRE(bytes.empty());
    }
}

TEST_CASE("a world whose manifest or heightmap is a directory is refused, not thrown at")
{
    TempDir dir("world");
    std::filesystem::create_directories(dir.path / "manifest.iworld");
    Heightfield field;
    REQUIRE_FALSE(readWorld(dir.path, field));
}

TEST_CASE("writers racing for one file leave it whole and no temporary files")
{
    TempDir dir("race");
    const std::vector<std::uint8_t> content(1 << 16, 0x5a);
    {
        std::vector<std::jthread> threads;
        for (int i = 0; i < 6; ++i) {
            threads.emplace_back([&] {
                for (int k = 0; k < 20; ++k) {
                    writeFileAtomically(dir.path / "target.bin", content);
                }
            });
        }
    }
    std::vector<std::uint8_t> bytes;
    REQUIRE(readFileBytes(dir.path / "target.bin", bytes));
    REQUIRE(bytes == content);
    for (const auto& entry : std::filesystem::directory_iterator(dir.path)) {
        REQUIRE(entry.path().filename() == "target.bin");
    }
}
