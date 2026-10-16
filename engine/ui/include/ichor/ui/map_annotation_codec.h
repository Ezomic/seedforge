#pragma once

#include "ichor/ui/map_annotation_store.h"

#include <cstdint>
#include <span>
#include <vector>

namespace ichor {

// Standalone byte form of a MapAnnotationStore. It is not part of any save
// format yet; a later ticket decides where these bytes live.
//
// Layout, all integers little-endian, floats as their IEEE-754 bit pattern:
//   "ICMA" | u8 version (1) | u32 count |
//   per annotation: u8 kind | u8 symbol | u16 idLen | id | u16 textLen | text |
//                   u16 pointCount | pointCount * (f32 x, f32 y)
// The same store always encodes to the same bytes.

enum class AnnotationDecodeStatus {
    Ok,
    BadMagic,
    BadVersion,
    Truncated,
    TrailingBytes,
    TooMany,       // the count exceeds the limits, or cannot fit in the bytes given
    InvalidRecord, // a record fails the same checks MapAnnotationStore::add makes
};

struct AnnotationDecodeResult {
    AnnotationDecodeStatus status = AnnotationDecodeStatus::Truncated;
    MapAnnotationStore store;
    bool ok() const { return status == AnnotationDecodeStatus::Ok; }
};

std::vector<std::uint8_t> encodeAnnotations(const MapAnnotationStore& store);

// Never throws or crashes on any input. On failure the returned store is empty.
AnnotationDecodeResult decodeAnnotations(std::span<const std::uint8_t> bytes, const AnnotationLimits& limits = {});

} // namespace ichor
