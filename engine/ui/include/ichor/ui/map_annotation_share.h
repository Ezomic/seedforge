#pragma once

#include "ichor/ui/map_annotation_codec.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Sharing a player-authored map with someone walking the same seed. The
// envelope wraps the codec bytes of one MapAnnotationStore; no transport is
// involved here. Layout, little-endian:
//   "ICMS" | u8 version (1) | u16 seedLen | seed | u16 authorLen | author |
//   u32 payloadLen | payload (encodeAnnotations bytes) | u32 crc32 of every
//   byte before it
// The same inputs always encode to the same bytes.

struct AnnotationShareLimits {
    std::size_t maxSeedBytes = 64;
    std::size_t maxAuthorBytes = 32;
    std::size_t maxEnvelopeBytes = 1u << 20;
};

enum class AnnotationShareStatus {
    Ok,
    InvalidSeed,   // empty, too long, bad UTF-8 or a control character
    InvalidAuthor, // too long, bad UTF-8 or a control character (empty is allowed)
    TooLarge,      // the envelope would exceed or does exceed maxEnvelopeBytes
    BadMagic,
    BadVersion,
    Truncated,
    BadChecksum,
    TrailingBytes,
    BadPayload, // the checksum held but the seed, author or annotation bytes are rejected
    WrongSeed,  // import only: the envelope is for a different seed
};

struct AnnotationShareEncoded {
    AnnotationShareStatus status = AnnotationShareStatus::InvalidSeed;
    std::vector<std::uint8_t> bytes;
};

struct AnnotationShareDecoded {
    AnnotationShareStatus status = AnnotationShareStatus::Truncated;
    std::string seed;
    std::string author;
    MapAnnotationStore store;
};

struct AnnotationImportReport {
    AnnotationShareStatus status = AnnotationShareStatus::Ok;
    std::size_t added = 0;
    std::size_t renamed = 0; // subset of added: the id was taken, a suffix was added
    std::size_t dropped = 0; // no room left in the own store, or no free id
};

std::uint32_t annotationCrc32(std::span<const std::uint8_t> bytes);

AnnotationShareEncoded encodeAnnotationShare(const MapAnnotationStore& store, std::string_view seed,
                                             std::string_view author, const AnnotationShareLimits& share = {});

// Never throws or crashes on any input. On failure the store is empty.
AnnotationShareDecoded decodeAnnotationShare(std::span<const std::uint8_t> bytes, const AnnotationLimits& limits = {},
                                             const AnnotationShareLimits& share = {});

// Adds each shared annotation to `own` in order. Existing annotations are
// never changed or removed. A colliding id gets "-2", "-3", ... appended
// (the stem is cut to fit maxIdBytes). When `own` is full the rest are dropped.
AnnotationImportReport mergeSharedAnnotations(MapAnnotationStore& own, const MapAnnotationStore& shared);

// Decode, require the envelope seed to equal `ownSeed`, then merge. `own` is
// untouched unless the status is Ok.
AnnotationImportReport importAnnotationShare(MapAnnotationStore& own, std::string_view ownSeed,
                                             std::span<const std::uint8_t> bytes,
                                             const AnnotationShareLimits& share = {});

} // namespace ichor
