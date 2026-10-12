#include "ichor/core/file_watch.h"

namespace ichor {

namespace {

std::filesystem::file_time_type stamp(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    return ec ? std::filesystem::file_time_type::min() : time;
}

}

void FileWatcher::watch(const std::filesystem::path& path)
{
    for (const Entry& entry : entries_) {
        if (entry.path == path) {
            return;
        }
    }
    entries_.push_back({path, stamp(path)});
}

void FileWatcher::watchDirectory(const std::filesystem::path& directory, std::string_view extension)
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            watch(entry.path());
        }
    }
}

std::vector<std::filesystem::path> FileWatcher::poll()
{
    std::vector<std::filesystem::path> changed;
    for (Entry& entry : entries_) {
        const auto now = stamp(entry.path);
        if (now != entry.seen) {
            entry.seen = now;
            changed.push_back(entry.path);
        }
    }
    return changed;
}

}
