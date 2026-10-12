#include "ichor/core/file_watch.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>

using namespace ichor;

TEST_CASE("the watcher reports a file once per change")
{
    const auto dir = std::filesystem::temp_directory_path() / "ichor_watch";
    std::filesystem::create_directories(dir);
    const auto file = dir / "a.spv";
    std::ofstream(file) << "one";

    FileWatcher watcher;
    watcher.watchDirectory(dir, ".spv");
    REQUIRE(watcher.poll().empty());

    // Push the timestamp forward explicitly; filesystems round and tests must not sleep.
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(2));
    const auto changed = watcher.poll();
    REQUIRE(changed.size() == 1);
    REQUIRE(changed[0] == file);
    REQUIRE(watcher.poll().empty());

    std::filesystem::remove_all(dir);
    REQUIRE(watcher.poll().size() == 1); // deletion counts as a change too
}
