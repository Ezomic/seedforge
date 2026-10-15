#include "ichor/core/file_watch.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>

using namespace ichor;

TEST_CASE("the watcher reports a file once per change")
{
    const auto dir = test::tempPath("ichor_watch");
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

TEST_CASE("a watched tree sees nested files, new files and new directories")
{
    const auto dir = test::tempPath("ichor_watch_tree");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "kits" / "deep");
    const auto nested = dir / "kits" / "deep" / "x.json";
    std::ofstream(nested) << "[]";
    std::ofstream(dir / "ignored.txt") << "no";

    FileWatcher watcher;
    watcher.watchTree(dir, ".json");
    REQUIRE(watcher.poll().empty());

    std::filesystem::last_write_time(nested, std::filesystem::last_write_time(nested) + std::chrono::seconds(2));
    REQUIRE(watcher.poll() == std::vector<std::filesystem::path>{nested});

    const auto fresh = dir / "kits" / "added.json";
    std::ofstream(fresh) << "[]";
    REQUIRE(watcher.poll() == std::vector<std::filesystem::path>{fresh});
    REQUIRE(watcher.poll().empty());

    std::filesystem::create_directories(dir / "tables");
    const auto table = dir / "tables" / "t.json";
    std::ofstream(table) << "[]";
    REQUIRE(watcher.poll() == std::vector<std::filesystem::path>{table});

    std::filesystem::remove_all(dir);
    REQUIRE(watcher.poll().size() == 3); // deletions count too, and a missing root does not throw
}
