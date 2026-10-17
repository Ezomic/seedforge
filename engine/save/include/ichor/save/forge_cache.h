#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace ichor {

// Guest-side cache of a locally forged world (docs/15-multiplayer.md, "The advantage already in
// hand"): the first join pays the forge, later joins read the result back. The payload is opaque
// bytes, so this knows nothing about what a forged world looks like and nothing here touches the
// network or the simulation.
//
// An entry is named by the seed, the generator version and the content digest the host announced
// (the three fields of WorldIdentity), so a payload forged under another version or one that does
// not match the host can never be served for the wrong world.

struct ForgeCacheKey {
    std::uint64_t seed = 0;
    std::uint32_t genVersion = 0;
    std::uint64_t contentDigest = 0;

    auto operator<=>(const ForgeCacheKey&) const = default;
};

struct ForgeCacheLimits {
    std::size_t maxEntries = 16;
    std::uint64_t maxTotalBytes = std::uint64_t{1} << 30; // payload bytes over all entries
    std::uint32_t maxPayloadBytes = std::uint32_t{1} << 29;
};

// One cache file. Layout, all integers little-endian:
//   "ICFC" | u8 version (1) | u64 sequence | u64 seed | u32 genVersion | u64 contentDigest
//   | u32 payloadLen | u64 checksum | payload
// The checksum is FNV-1a over every byte before it followed by the payload, so a flipped bit
// anywhere in the file is caught. The same record always encodes to the same bytes.
struct ForgeCacheRecord {
    std::uint64_t sequence = 0; // insertion order; the lowest is evicted first
    ForgeCacheKey key;
    std::vector<std::uint8_t> payload;
};

enum class ForgeRecordStatus {
    Ok,
    Truncated,
    BadMagic,
    BadVersion,
    TooLarge, // the payload length exceeds the limit
    BadChecksum,
    TrailingBytes,
};

struct ForgeRecordResult {
    ForgeRecordStatus status = ForgeRecordStatus::Truncated;
    ForgeCacheRecord record;
    bool ok() const { return status == ForgeRecordStatus::Ok; }
};

std::vector<std::uint8_t> encodeForgeRecord(const ForgeCacheRecord& record);

// Never throws or crashes on any input. On failure the returned record is empty.
ForgeRecordResult decodeForgeRecord(std::span<const std::uint8_t> bytes, std::uint32_t maxPayloadBytes = ForgeCacheLimits{}.maxPayloadBytes);

enum class ForgePutStatus {
    Stored,
    PayloadTooLarge, // over maxPayloadBytes or maxTotalBytes, or the cache holds no entries; untouched
    IoFailed,        // the write failed; the cache is untouched
};

enum class ForgeGetStatus {
    Hit,
    Miss,
    Corrupt, // the file failed validation and has been deleted; treat as a miss and forge again
};

struct ForgeGetResult {
    ForgeGetStatus status = ForgeGetStatus::Miss;
    std::vector<std::uint8_t> payload;
};

struct ForgeScanReport {
    std::size_t kept = 0;
    std::size_t discarded = 0; // invalid, mismatched or over-limit files that were deleted
    std::size_t evicted = 0;   // valid files removed to fit the limits
};

// A folder of cache files. Only files this class names are read or deleted, so a shared folder is
// safe. Eviction is first in, first out by insertion sequence with the key as tie break, so the
// same history always leaves the same entries, whatever the file system returns or the clock says.
class ForgeCache {
public:
    explicit ForgeCache(std::filesystem::path directory, ForgeCacheLimits limits = {});

    // Rebuilds the index from disk: deletes stale temp files and any file that is corrupt or named
    // for another key, then evicts down to the limits. Call once after construction.
    ForgeScanReport scan();

    ForgePutStatus put(const ForgeCacheKey& key, std::span<const std::uint8_t> payload);
    // Reads and re-validates the file, so corruption since scan() is caught here.
    ForgeGetResult get(const ForgeCacheKey& key);
    bool contains(const ForgeCacheKey& key) const { return entries_.contains(key); }

    // Keys in eviction order, next to go first.
    std::vector<ForgeCacheKey> keysByAge() const;
    std::size_t size() const { return entries_.size(); }
    std::uint64_t totalBytes() const { return totalBytes_; }

    std::filesystem::path pathFor(const ForgeCacheKey& key) const;

private:
    struct Entry {
        std::uint64_t sequence = 0;
        std::uint64_t payloadBytes = 0;
    };

    std::size_t evictToFit(const ForgeCacheKey* keep);
    // False when the file could not be deleted; the entry is then kept so the next eviction retries.
    bool drop(const ForgeCacheKey& key);

    std::filesystem::path directory_;
    ForgeCacheLimits limits_;
    std::map<ForgeCacheKey, Entry> entries_;
    std::uint64_t totalBytes_ = 0;
    std::uint64_t nextSequence_ = 1;
};

} // namespace ichor
