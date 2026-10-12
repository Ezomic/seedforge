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

void FileWatcher::watchTree(const std::filesystem::path& directory, std::string_view extension)
{
    trees_.push_back({directory, std::string(extension)});
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == extension) {
            watch(it->path());
        }
    }
}

std::vector<std::filesystem::path> FileWatcher::poll()
{
    std::vector<std::filesystem::path> changed;
    for (const Tree& tree : trees_) {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(tree.root, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != tree.extension) {
                continue;
            }
            bool known = false;
            for (const Entry& entry : entries_) {
                known = known || entry.path == it->path();
            }
            if (!known) {
                entries_.push_back({it->path(), stamp(it->path())});
                changed.push_back(it->path());
            }
        }
    }
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
