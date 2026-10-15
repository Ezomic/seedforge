#include "ichor/save/atomic_file.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ichor {

namespace fs = std::filesystem;

namespace {

bool flushToDisk(std::FILE* file)
{
    if (std::fflush(file) != 0) {
        return false;
    }
#ifdef _WIN32
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

// A rename is only durable once the folder entry itself is on disk.
void syncDirectory(const fs::path& directory)
{
#ifndef _WIN32
    const int fd = open(directory.c_str(), O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
#else
    (void)directory;
#endif
}

std::FILE* openForWrite(const fs::path& path)
{
#ifdef _WIN32
    return _wfopen(path.c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

// False when the bytes could not be written or flushed; partial stops after
// half the bytes without flushing, which is what a killed process leaves.
bool writeTemp(const fs::path& path, std::span<const std::uint8_t> bytes, bool partial)
{
    std::FILE* file = openForWrite(path);
    if (!file) {
        return false;
    }
    const std::size_t count = partial ? bytes.size() / 2 : bytes.size();
    bool ok = count == 0 || std::fwrite(bytes.data(), 1, count, file) == count;
    if (ok && !partial) {
        ok = flushToDisk(file);
    } else if (partial) {
        ok = std::fflush(file) == 0;
    }
    return std::fclose(file) == 0 && ok;
}

bool readAll(const fs::path& path, std::uintmax_t maxBytes, std::vector<std::uint8_t>& bytes)
{
    bytes.clear();
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return false;
    }
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec || size == 0 || size > maxBytes) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(in.gcount()) != size) {
        bytes.clear();
        return false;
    }
    return true;
}

bool currentIsGood(const fs::path& target, const AtomicWriteOptions& options)
{
    std::vector<std::uint8_t> current;
    if (!readAll(target, std::uintmax_t{1} << 30, current)) {
        return false;
    }
    return !options.valid || options.valid(current);
}

void rotateHistory(const fs::path& target, std::uint32_t backups)
{
    std::error_code ec;
    for (std::uint32_t i = backups - 1; i >= 1; --i) {
        const fs::path from = backupPath(target, i);
        if (fs::exists(from, ec)) {
            fs::rename(from, backupPath(target, i + 1), ec);
        }
    }
    fs::copy_file(target, backupPath(target, 1), fs::copy_options::overwrite_existing, ec);
}

} // namespace

fs::path backupPath(const fs::path& target, std::uint32_t index)
{
    return target.parent_path() / (target.stem().string() + ".bak" + std::to_string(index));
}

fs::path tempPath(const fs::path& target)
{
    return target.parent_path() / (target.filename().string() + ".tmp");
}

AtomicWriteResult writeAtomic(const fs::path& target, std::span<const std::uint8_t> bytes, const AtomicWriteOptions& options)
{
    std::error_code ec;
    const fs::path directory = target.has_parent_path() ? target.parent_path() : fs::path(".");
    if (!fs::is_directory(directory, ec)) {
        return { AtomicWriteStatus::NoDirectory };
    }

    const bool exists = fs::exists(target, ec);
    if (exists) {
        const fs::perms perms = fs::status(target, ec).permissions();
        constexpr fs::perms kWrite = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
        if (!ec && (perms & kWrite) == fs::perms::none) {
            return { AtomicWriteStatus::ReadOnly };
        }
    }

    const fs::path temp = tempPath(target);
    const bool partial = options.interruptAt == AtomicStage::TempPartial;
    if (!writeTemp(temp, bytes, partial)) {
        fs::remove(temp, ec);
        return { AtomicWriteStatus::WriteFailed };
    }
    if (options.interruptAt == AtomicStage::TempPartial || options.interruptAt == AtomicStage::TempComplete) {
        return { AtomicWriteStatus::Interrupted };
    }

    if (options.backups > 0 && exists && currentIsGood(target, options)) {
        rotateHistory(target, options.backups);
    }
    if (options.interruptAt == AtomicStage::BackupsRotated) {
        return { AtomicWriteStatus::Interrupted };
    }

    fs::rename(temp, target, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return { AtomicWriteStatus::RenameFailed };
    }
    syncDirectory(directory);
    return { AtomicWriteStatus::Ok };
}

RecoveredFile recoverNewestValid(const fs::path& target, std::uint32_t backups,
    const std::function<bool(std::span<const std::uint8_t>)>& valid, std::uintmax_t maxBytes)
{
    RecoveredFile result;
    std::vector<std::uint8_t> bytes;
    for (std::uint32_t i = 0; i <= backups; ++i) {
        const fs::path path = i == 0 ? target : backupPath(target, i);
        if (readAll(path, maxBytes, bytes) && (!valid || valid(bytes))) {
            result.found = true;
            result.source = i;
            result.bytes = std::move(bytes);
            return result;
        }
    }
    return result;
}

} // namespace ichor
