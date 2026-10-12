#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Polls modification times. Hot reload needs nothing fancier than this, and
// polling behaves the same on every platform and in every test.
class FileWatcher {
public:
    void watch(const std::filesystem::path& path);
    void watchDirectory(const std::filesystem::path& directory, std::string_view extension);
    // Every file with the extension under the directory, at any depth. The tree
    // is scanned again on every poll, so files added later are reported too.
    void watchTree(const std::filesystem::path& directory, std::string_view extension);

    // Paths whose modification time changed since the last poll (or since
    // they were added).
    std::vector<std::filesystem::path> poll();

private:
    struct Entry {
        std::filesystem::path path;
        std::filesystem::file_time_type seen;
    };
    struct Tree {
        std::filesystem::path root;
        std::string extension;
    };
    std::vector<Entry> entries_;
    std::vector<Tree> trees_;
};

}
