#include "ichor/save/forge_cache.h"

#include "ichor/core/hash.h"
#include "ichor/save/atomic_file.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace ichor {

namespace {

namespace fs = std::filesystem;

constexpr std::uint8_t kMagic[4] = {'I', 'C', 'F', 'C'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kChecksumOffset = 37;
constexpr std::size_t kHeaderBytes = 45;
constexpr std::string_view kExtension = ".icfc";
// A sequence this high only comes from a damaged or forged file; honouring it would stop every later put.
constexpr std::uint64_t kMaxSequence = std::uint64_t{1} << 62;

void putLe(std::vector<std::uint8_t>& out, std::uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

std::uint64_t getLe(std::span<const std::uint8_t> bytes, std::size_t pos, int count)
{
    std::uint64_t value = 0;
    for (int i = 0; i < count; ++i) {
        value |= static_cast<std::uint64_t>(bytes[pos + static_cast<std::size_t>(i)]) << (8 * i);
    }
    return value;
}

std::uint64_t checksumOf(std::span<const std::uint8_t> head, std::span<const std::uint8_t> payload)
{
    const auto view = [](std::span<const std::uint8_t> s) { return std::string_view(reinterpret_cast<const char*>(s.data()), s.size()); };
    return hash64(view(payload), hash64(view(head)));
}

std::string fileName(const ForgeCacheKey& key)
{
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%016llx-%08x-%016llx", static_cast<unsigned long long>(key.seed), static_cast<unsigned>(key.genVersion),
        static_cast<unsigned long long>(key.contentDigest));
    return std::string(buffer) + std::string(kExtension);
}

bool isLowerHex(std::string_view text)
{
    return std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// Accepts exactly the names fileName produces.
bool isCacheFileName(const std::string& name)
{
    constexpr std::size_t kStem = 16 + 1 + 8 + 1 + 16;
    return name.size() == kStem + kExtension.size() && name.ends_with(kExtension) && name[16] == '-' && name[25] == '-'
        && isLowerHex(std::string_view(name).substr(0, 16)) && isLowerHex(std::string_view(name).substr(17, 8))
        && isLowerHex(std::string_view(name).substr(26, 16));
}

// The record at `path` if it is valid and was written for exactly `key` (when given).
std::optional<ForgeCacheRecord> readRecord(const fs::path& path, const ForgeCacheLimits& limits, const ForgeCacheKey* key)
{
    const std::uintmax_t maxFile = kHeaderBytes + static_cast<std::uintmax_t>(limits.maxPayloadBytes);
    const auto found = recoverNewestValid(path, 0, {}, maxFile);
    if (!found.found) {
        return std::nullopt;
    }
    auto decoded = decodeForgeRecord(found.bytes, limits.maxPayloadBytes);
    if (!decoded.ok() || (key != nullptr && decoded.record.key != *key)) {
        return std::nullopt;
    }
    return std::move(decoded.record);
}

} // namespace

std::vector<std::uint8_t> encodeForgeRecord(const ForgeCacheRecord& record)
{
    std::vector<std::uint8_t> out(std::begin(kMagic), std::end(kMagic));
    out.push_back(kVersion);
    putLe(out, record.sequence, 8);
    putLe(out, record.key.seed, 8);
    putLe(out, record.key.genVersion, 4);
    putLe(out, record.key.contentDigest, 8);
    putLe(out, record.payload.size(), 4);
    putLe(out, checksumOf(out, record.payload), 8);
    out.insert(out.end(), record.payload.begin(), record.payload.end());
    return out;
}

ForgeRecordResult decodeForgeRecord(std::span<const std::uint8_t> bytes, std::uint32_t maxPayloadBytes)
{
    const auto fail = [](ForgeRecordStatus status) { return ForgeRecordResult{status, {}}; };
    if (bytes.size() < kHeaderBytes) {
        return fail(ForgeRecordStatus::Truncated);
    }
    if (std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
        return fail(ForgeRecordStatus::BadMagic);
    }
    if (bytes[4] != kVersion) {
        return fail(ForgeRecordStatus::BadVersion);
    }
    const std::uint64_t length = getLe(bytes, 33, 4);
    if (length > maxPayloadBytes) {
        return fail(ForgeRecordStatus::TooLarge);
    }
    const std::size_t have = bytes.size() - kHeaderBytes;
    if (have < length) {
        return fail(ForgeRecordStatus::Truncated);
    }
    if (have > length) {
        return fail(ForgeRecordStatus::TrailingBytes);
    }
    const auto payload = bytes.subspan(kHeaderBytes);
    if (checksumOf(bytes.first(kChecksumOffset), payload) != getLe(bytes, kChecksumOffset, 8)) {
        return fail(ForgeRecordStatus::BadChecksum);
    }
    ForgeCacheRecord record;
    record.sequence = getLe(bytes, 5, 8);
    record.key = {getLe(bytes, 13, 8), static_cast<std::uint32_t>(getLe(bytes, 21, 4)), getLe(bytes, 25, 8)};
    record.payload.assign(payload.begin(), payload.end());
    return {ForgeRecordStatus::Ok, std::move(record)};
}

ForgeCache::ForgeCache(fs::path directory, ForgeCacheLimits limits)
    : directory_(std::move(directory))
    , limits_(limits)
{
}

fs::path ForgeCache::pathFor(const ForgeCacheKey& key) const
{
    return directory_ / fileName(key);
}

bool ForgeCache::drop(const ForgeCacheKey& key)
{
    std::error_code ec;
    fs::remove(pathFor(key), ec);
    if (ec) {
        return false;
    }
    const auto it = entries_.find(key);
    if (it != entries_.end()) {
        totalBytes_ -= it->second.payloadBytes;
        entries_.erase(it);
    }
    return true;
}

std::vector<ForgeCacheKey> ForgeCache::keysByAge() const
{
    std::vector<ForgeCacheKey> keys;
    keys.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        keys.push_back(key);
    }
    std::ranges::sort(keys, [this](const ForgeCacheKey& a, const ForgeCacheKey& b) {
        const auto sa = entries_.at(a).sequence;
        const auto sb = entries_.at(b).sequence;
        return sa != sb ? sa < sb : a < b;
    });
    return keys;
}

std::size_t ForgeCache::evictToFit(const ForgeCacheKey* keep)
{
    std::size_t evicted = 0;
    for (const auto& key : keysByAge()) {
        if (entries_.size() <= limits_.maxEntries && totalBytes_ <= limits_.maxTotalBytes) {
            break;
        }
        if (keep != nullptr && key == *keep) {
            continue;
        }
        if (drop(key)) {
            ++evicted;
        }
    }
    return evicted;
}

ForgeScanReport ForgeCache::scan()
{
    ForgeScanReport report;
    entries_.clear();
    totalBytes_ = 0;
    nextSequence_ = 1;

    std::error_code ec;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(directory_, ec), end; !ec && it != end; it.increment(ec)) {
        files.push_back(it->path());
    }
    std::ranges::sort(files);

    for (const auto& path : files) {
        const std::string name = path.filename().string();
        std::error_code rc;
        if (name.ends_with(".tmp") && isCacheFileName(name.substr(0, name.size() - 4))) {
            fs::remove(path, rc);
            ++report.discarded;
            continue;
        }
        if (!isCacheFileName(name)) {
            continue;
        }
        auto record = readRecord(path, limits_, nullptr);
        if (!record || pathFor(record->key).filename() != name || record->sequence > kMaxSequence || record->payload.size() > limits_.maxTotalBytes) {
            fs::remove(path, rc);
            ++report.discarded;
            continue;
        }
        entries_[record->key] = {record->sequence, record->payload.size()};
        totalBytes_ += record->payload.size();
        nextSequence_ = std::max(nextSequence_, record->sequence + 1);
    }
    report.evicted = evictToFit(nullptr);
    report.kept = entries_.size();
    return report;
}

ForgePutStatus ForgeCache::put(const ForgeCacheKey& key, std::span<const std::uint8_t> payload)
{
    if (payload.size() > limits_.maxPayloadBytes || payload.size() > limits_.maxTotalBytes || limits_.maxEntries == 0) {
        return ForgePutStatus::PayloadTooLarge;
    }
    if (nextSequence_ > kMaxSequence) {
        return ForgePutStatus::IoFailed;
    }
    ForgeCacheRecord record{nextSequence_, key, {payload.begin(), payload.end()}};
    AtomicWriteOptions options;
    options.backups = 0;
    if (!writeAtomic(pathFor(key), encodeForgeRecord(record), options).ok()) {
        return ForgePutStatus::IoFailed;
    }
    ++nextSequence_;
    const auto old = entries_.find(key);
    if (old != entries_.end()) {
        totalBytes_ -= old->second.payloadBytes;
    }
    entries_[key] = {record.sequence, payload.size()};
    totalBytes_ += payload.size();
    evictToFit(&key);
    return ForgePutStatus::Stored;
}

ForgeGetResult ForgeCache::get(const ForgeCacheKey& key)
{
    if (!entries_.contains(key)) {
        return {};
    }
    auto record = readRecord(pathFor(key), limits_, &key);
    if (!record) {
        const bool vanished = !fs::exists(pathFor(key));
        drop(key);
        return {vanished ? ForgeGetStatus::Miss : ForgeGetStatus::Corrupt, {}};
    }
    return {ForgeGetStatus::Hit, std::move(record->payload)};
}

} // namespace ichor
