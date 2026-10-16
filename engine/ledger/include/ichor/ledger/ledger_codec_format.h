#pragma once

#include "ichor/ledger/consequence_ledger.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ichor {

// A compact, deterministic saved form of the ledger (doc 14): subjects, kinds
// and values are stored once in a string table, integers are varints and each
// tick is stored as its difference from the one before. It is separate from
// ConsequenceLedger::serialize(), whose bytes are unchanged.
//
// Layout: version byte, varint string count, the strings (varint length then
// bytes, in order of first use), varint fact count, then per fact: id (varint
// length then bytes), three varint string indices (subject, kind, value) and
// the zigzag varint tick difference.
constexpr std::uint8_t kLedgerCodecVersion = 1;

std::vector<std::uint8_t> encodeLedger(const ConsequenceLedger& ledger);

// All or nothing. Applies the same rules as ConsequenceLedger::append(), and
// never reserves more than the bytes could hold, so hostile counts cost nothing.
std::optional<ConsequenceLedger> decodeLedger(std::span<const std::uint8_t> bytes);

}
