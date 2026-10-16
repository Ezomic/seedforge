#pragma once

#include "ichor/save/codex_store.h"

#include <cstdint>
#include <span>
#include <vector>

namespace ichor {

// Standalone byte form of a CodexStore. It is a new format and not part of any
// save file yet; a later ticket decides where these bytes live.
//
// Layout, all integers little-endian:
//   "ICCX" | u8 version (1) | u32 count | per entry: u8 category | u8 idLen | id
// The same store always encodes to the same bytes. Unknown versions are rejected.

enum class CodexDecodeStatus {
    Ok,
    BadMagic,
    BadVersion,
    Truncated,
    TrailingBytes,
    TooMany,       // the count exceeds the limits, or cannot fit in the bytes given
    InvalidRecord, // bad category or id, a duplicate, or a per-category cap exceeded
};

struct CodexDecodeResult {
    CodexDecodeStatus status = CodexDecodeStatus::Truncated;
    CodexStore store;
    bool ok() const { return status == CodexDecodeStatus::Ok; }
};

std::vector<std::uint8_t> encodeCodex(const CodexStore& store);

// Never throws or crashes on any input. On failure the returned store is empty.
CodexDecodeResult decodeCodex(std::span<const std::uint8_t> bytes, const CodexLimits& limits = {});

} // namespace ichor
