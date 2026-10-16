#include "ichor/save/codex_codec.h"

#include <cstring>

namespace ichor {

namespace {

constexpr std::uint8_t kMagic[4] = {'I', 'C', 'C', 'X'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 9;
// category and idLen; a valid id adds at least one byte.
constexpr std::size_t kMinRecordBytes = 3;

} // namespace

std::vector<std::uint8_t> encodeCodex(const CodexStore& store)
{
    std::vector<std::uint8_t> out(std::begin(kMagic), std::end(kMagic));
    out.push_back(kVersion);
    const auto count = static_cast<std::uint32_t>(store.size());
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(count >> (8 * i)));
    }
    for (const auto& e : store.all()) {
        out.push_back(static_cast<std::uint8_t>(e.category));
        out.push_back(static_cast<std::uint8_t>(e.id.size()));
        out.insert(out.end(), e.id.begin(), e.id.end());
    }
    return out;
}

CodexDecodeResult decodeCodex(std::span<const std::uint8_t> bytes, const CodexLimits& limits)
{
    const auto fail = [&](CodexDecodeStatus status) { return CodexDecodeResult{status, CodexStore(limits)}; };
    if (bytes.size() < kHeaderBytes) {
        return fail(CodexDecodeStatus::Truncated);
    }
    if (std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
        return fail(CodexDecodeStatus::BadMagic);
    }
    if (bytes[4] != kVersion) {
        return fail(CodexDecodeStatus::BadVersion);
    }
    std::uint32_t count = 0;
    for (int i = 0; i < 4; ++i) {
        count |= static_cast<std::uint32_t>(bytes[5 + static_cast<std::size_t>(i)]) << (8 * i);
    }
    const std::size_t maxEntries = limits.maxEntriesPerCategory * static_cast<std::size_t>(CodexCategory::Count_);
    if (count > maxEntries || static_cast<std::uint64_t>(count) * kMinRecordBytes > bytes.size() - kHeaderBytes) {
        return fail(CodexDecodeStatus::TooMany);
    }

    CodexStore store(limits);
    std::size_t pos = kHeaderBytes;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (bytes.size() - pos < 2) {
            return fail(CodexDecodeStatus::Truncated);
        }
        const auto category = static_cast<CodexCategory>(bytes[pos]);
        const std::size_t length = bytes[pos + 1];
        pos += 2;
        if (bytes.size() - pos < length) {
            return fail(CodexDecodeStatus::Truncated);
        }
        const std::string_view id(reinterpret_cast<const char*>(bytes.data() + pos), length);
        pos += length;
        if (store.unlock(category, id) != CodexResult::Unlocked) {
            return fail(CodexDecodeStatus::InvalidRecord);
        }
    }
    if (pos != bytes.size()) {
        return fail(CodexDecodeStatus::TrailingBytes);
    }
    return {CodexDecodeStatus::Ok, std::move(store)};
}

} // namespace ichor
