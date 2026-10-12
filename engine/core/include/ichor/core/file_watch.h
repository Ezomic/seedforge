#pragma once

#include <filesystem>
#include <vector>

namespace ichor {

// Polls modification times. Hot reload needs nothing fancier than this, and
// polling behaves the same on every platform and in every test.
class FileWatcher {
public:
    void watch(const std::filesystem::path& path);
    void watchDirectory(const std::filesystem::path& directory, std::string_view extension);

    // Paths whose modification time changed since the last poll (or since
    // they were added).
    std::vector<std::filesystem::path> poll();

private:
    struct Entry {
        std::filesystem::path path;
        std::filesystem::file_time_type seen;
    };
    std::vector<Entry> entries_;
};

}
