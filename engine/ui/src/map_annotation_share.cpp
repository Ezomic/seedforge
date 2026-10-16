#include "ichor/ui/map_annotation_share.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

namespace ichor {

namespace {

constexpr std::uint8_t kMagic[4] = {'I', 'C', 'M', 'S'};
constexpr std::uint8_t kVersion = 1;
// magic, version, seedLen, authorLen, payloadLen, crc with everything else empty.
constexpr std::size_t kMinBytes = 4 + 1 + 2 + 2 + 4 + 4;
constexpr std::size_t kMaxLengthField = 0xFFFF;

std::array<std::uint32_t, 256> makeCrcTable()
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

std::uint32_t get32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t get16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

AnnotationShareDecoded failDecode(AnnotationShareStatus status, const AnnotationLimits& limits)
{
    return {status, {}, {}, MapAnnotationStore(limits)};
}

} // namespace

std::uint32_t annotationCrc32(std::span<const std::uint8_t> bytes)
{
    static const std::array<std::uint32_t, 256> table = makeCrcTable();
    std::uint32_t c = 0xFFFFFFFFu;
    for (const std::uint8_t b : bytes) {
        c = table[(c ^ b) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

AnnotationShareEncoded encodeAnnotationShare(const MapAnnotationStore& store, std::string_view seed,
                                             std::string_view author, const AnnotationShareLimits& share)
{
    if (seed.empty() || seed.size() > share.maxSeedBytes || seed.size() > kMaxLengthField ||
        !isCleanAnnotationText(seed)) {
        return {AnnotationShareStatus::InvalidSeed, {}};
    }
    if (author.size() > share.maxAuthorBytes || author.size() > kMaxLengthField || !isCleanAnnotationText(author)) {
        return {AnnotationShareStatus::InvalidAuthor, {}};
    }
    const std::vector<std::uint8_t> payload = encodeAnnotations(store);
    const std::size_t total = kMinBytes + seed.size() + author.size() + payload.size();
    if (total > share.maxEnvelopeBytes) {
        return {AnnotationShareStatus::TooLarge, {}};
    }

    std::vector<std::uint8_t> out(std::begin(kMagic), std::end(kMagic));
    out.reserve(total);
    out.push_back(kVersion);
    put16(out, static_cast<std::uint16_t>(seed.size()));
    out.insert(out.end(), seed.begin(), seed.end());
    put16(out, static_cast<std::uint16_t>(author.size()));
    out.insert(out.end(), author.begin(), author.end());
    put32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    put32(out, annotationCrc32(out));
    return {AnnotationShareStatus::Ok, std::move(out)};
}

AnnotationShareDecoded decodeAnnotationShare(std::span<const std::uint8_t> bytes, const AnnotationLimits& limits,
                                             const AnnotationShareLimits& share)
{
    if (bytes.size() > share.maxEnvelopeBytes) {
        return failDecode(AnnotationShareStatus::TooLarge, limits);
    }
    if (bytes.size() < kMinBytes) {
        return failDecode(AnnotationShareStatus::Truncated, limits);
    }
    if (std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
        return failDecode(AnnotationShareStatus::BadMagic, limits);
    }
    if (bytes[4] != kVersion) {
        return failDecode(AnnotationShareStatus::BadVersion, limits);
    }
    const std::size_t crcAt = bytes.size() - 4;
    if (annotationCrc32(bytes.first(crcAt)) != get32(bytes.data() + crcAt)) {
        return failDecode(AnnotationShareStatus::BadChecksum, limits);
    }

    // A sender can compute a valid checksum over hostile lengths, so every
    // length is still bounds-checked against the bytes that remain.
    std::size_t pos = 5;
    const std::size_t end = crcAt;
    const std::size_t seedLen = get16(bytes.data() + pos);
    pos += 2;
    if (seedLen > share.maxSeedBytes || end - pos < seedLen + 2) {
        return failDecode(AnnotationShareStatus::Truncated, limits);
    }
    AnnotationShareDecoded result = failDecode(AnnotationShareStatus::Ok, limits);
    result.seed.assign(reinterpret_cast<const char*>(bytes.data() + pos), seedLen);
    pos += seedLen;
    const std::size_t authorLen = get16(bytes.data() + pos);
    pos += 2;
    if (authorLen > share.maxAuthorBytes || end - pos < authorLen + 4) {
        return failDecode(AnnotationShareStatus::Truncated, limits);
    }
    result.author.assign(reinterpret_cast<const char*>(bytes.data() + pos), authorLen);
    pos += authorLen;
    const std::size_t payloadLen = get32(bytes.data() + pos);
    pos += 4;
    if (payloadLen > end - pos) {
        return failDecode(AnnotationShareStatus::Truncated, limits);
    }
    if (payloadLen < end - pos) {
        return failDecode(AnnotationShareStatus::TrailingBytes, limits);
    }
    if (result.seed.empty() || !isCleanAnnotationText(result.seed) || !isCleanAnnotationText(result.author)) {
        return failDecode(AnnotationShareStatus::BadPayload, limits);
    }
    AnnotationDecodeResult payload = decodeAnnotations(bytes.subspan(pos, payloadLen), limits);
    if (!payload.ok()) {
        return failDecode(AnnotationShareStatus::BadPayload, limits);
    }
    result.store = std::move(payload.store);
    return result;
}

AnnotationImportReport mergeSharedAnnotations(MapAnnotationStore& own, const MapAnnotationStore& shared)
{
    AnnotationImportReport report;
    for (const MapAnnotation& source : shared.all()) {
        if (own.size() >= own.limits().maxAnnotations) {
            ++report.dropped;
            continue;
        }
        MapAnnotation copy = source;
        bool renamed = false;
        if (own.find(copy.id) != nullptr) {
            const std::size_t maxId = own.limits().maxIdBytes;
            bool found = false;
            for (std::size_t n = 2; n <= own.size() + 2 && !found; ++n) {
                const std::string suffix = "-" + std::to_string(n);
                if (suffix.size() >= maxId) {
                    break;
                }
                std::string candidate = source.id.substr(0, maxId - suffix.size()) + suffix;
                if (own.find(candidate) == nullptr) {
                    copy.id = std::move(candidate);
                    found = true;
                }
            }
            if (!found) {
                ++report.dropped;
                continue;
            }
            renamed = true;
        }
        if (own.add(std::move(copy)) == AnnotationResult::Ok) {
            ++report.added;
            report.renamed += renamed ? 1 : 0;
        } else {
            ++report.dropped;
        }
    }
    return report;
}

AnnotationImportReport importAnnotationShare(MapAnnotationStore& own, std::string_view ownSeed,
                                             std::span<const std::uint8_t> bytes, const AnnotationShareLimits& share)
{
    AnnotationShareDecoded decoded = decodeAnnotationShare(bytes, own.limits(), share);
    if (decoded.status != AnnotationShareStatus::Ok) {
        return {decoded.status, 0, 0, 0};
    }
    if (decoded.seed != ownSeed) {
        return {AnnotationShareStatus::WrongSeed, 0, 0, 0};
    }
    return mergeSharedAnnotations(own, decoded.store);
}

} // namespace ichor
