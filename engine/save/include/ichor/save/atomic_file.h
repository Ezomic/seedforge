#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace ichor {

// Crash-safe file replacement. The new bytes go to "<name>.tmp", are flushed to
// disk, and only then renamed over the target, so a reader sees the old file or
// the new one and never a half-written one. Before the swap the current file
// (if it is still good) is copied into a rotating history "<stem>.bak1",
// "<stem>.bak2", ... with bak1 the newest.

enum class AtomicWriteStatus {
    Ok,
    NoDirectory,  // the folder the file belongs in does not exist; it is not created
    ReadOnly,     // the target is marked read-only; it is left alone
    WriteFailed,  // the temp file could not be written or flushed
    RenameFailed, // the temp file is complete but could not be moved into place
    Interrupted,  // a test hook stopped the write part-way, as a crash would
};

// Where a simulated crash lands. Only tests set this; real writes use None.
enum class AtomicStage {
    None,
    TempPartial,    // half the bytes are in the temp file, nothing flushed
    TempComplete,   // the temp file is whole and flushed, the target untouched
    BackupsRotated, // history shifted and the old file copied, the swap not yet done
};

struct AtomicWriteOptions {
    std::uint32_t backups = 3;
    // The current file is only copied into the history when this accepts it, so
    // a corrupt file never pushes a good backup out. Null means "not empty".
    std::function<bool(std::span<const std::uint8_t>)> valid;
    AtomicStage interruptAt = AtomicStage::None;
};

struct AtomicWriteResult {
    AtomicWriteStatus status = AtomicWriteStatus::WriteFailed;
    bool ok() const { return status == AtomicWriteStatus::Ok; }
};

std::filesystem::path backupPath(const std::filesystem::path& target, std::uint32_t index);
std::filesystem::path tempPath(const std::filesystem::path& target);

AtomicWriteResult writeAtomic(const std::filesystem::path& target, std::span<const std::uint8_t> bytes, const AtomicWriteOptions& options = {});

struct RecoveredFile {
    bool found = false;
    std::uint32_t source = 0; // 0 is the file itself, n is backup n
    std::vector<std::uint8_t> bytes;
};

// The newest of the file and its backups that is non-empty, no larger than
// maxBytes, and accepted by valid. Stale temp files are never considered.
RecoveredFile recoverNewestValid(const std::filesystem::path& target, std::uint32_t backups,
    const std::function<bool(std::span<const std::uint8_t>)>& valid = {}, std::uintmax_t maxBytes = std::uintmax_t{1} << 30);

} // namespace ichor
