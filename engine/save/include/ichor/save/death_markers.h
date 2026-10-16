#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ichor {

// Where players on one seed died, shared as an asynchronous danger map (doc 15, tier 1).
// Pure data: no drawing, no networking. Positions are integer centimetres so the set
// hashes and merges identically on every platform.

enum class DeathCause : std::uint8_t { Unknown, Fall, Combat, Hazard, Starvation, Drowning, Count_ };

// Largest absolute coordinate accepted, 1000 km. Keeps the cell maths far from overflow.
constexpr std::int32_t kDeathMarkerMaxCoord = 100000000;
// Side of a density cell in the ground plane: 16 m.
constexpr std::int32_t kDeathMarkerCellCm = 1600;
// Hard ceiling on the maxTotal a set or decoded blob may claim, so hostile bytes cannot ask for memory.
constexpr std::uint32_t kDeathMarkerMaxTotalCeiling = 65536;

struct DeathMarker {
    std::int32_t xCm = 0;
    std::int32_t yCm = 0;
    std::int32_t zCm = 0;
    DeathCause cause = DeathCause::Unknown;
    std::uint32_t day = 0; // world day of the death; newer markers win a full cell
    bool operator==(const DeathMarker&) const = default;
};

struct DeathMarkerLimits {
    std::uint32_t maxPerCell = 4;
    std::uint32_t maxTotal = 1024;
    bool operator==(const DeathMarkerLimits&) const = default;
};

bool isValidDeathMarker(const DeathMarker& marker);

enum class DeathAddResult {
    Added,
    Duplicate,
    Invalid, // coordinate out of range or unknown cause
    Dropped, // valid, but a density cap kept it out
};

enum class DeathMergeResult {
    Ok,
    SeedMismatch,
    LimitsMismatch, // commutativity needs both sides to cap identically
};

// A set of markers for one seed. The contents are a pure function of the set of valid
// markers ever offered: add and merge are commutative, associative and idempotent. When a
// cell (or the whole set) is full, the newest markers win, ties broken by a fixed total
// order, so the surviving markers do not depend on arrival order. Limits are clamped to
// maxPerCell >= 1 and 1 <= maxTotal <= kDeathMarkerMaxTotalCeiling.
class DeathMarkerSet {
public:
    explicit DeathMarkerSet(std::uint64_t seed, DeathMarkerLimits limits = {});

    DeathAddResult add(const DeathMarker& marker);
    DeathMergeResult merge(const DeathMarkerSet& other);

    std::uint64_t seed() const { return seed_; }
    const DeathMarkerLimits& limits() const { return limits_; }
    // Canonical order: by cell, then by fields.
    const std::vector<DeathMarker>& all() const { return markers_; }
    std::size_t size() const { return markers_.size(); }
    bool operator==(const DeathMarkerSet&) const = default;

private:
    friend struct DeathMarkerSetAccess;
    void enforceCaps();

    std::uint64_t seed_;
    DeathMarkerLimits limits_;
    std::vector<DeathMarker> markers_;
};

// Layout, all integers little-endian:
//   "ICDM" | u8 version (1) | u64 seed | u32 maxPerCell | u32 maxTotal | u32 count
//   | per marker: i32 x | i32 y | i32 z | u8 cause | u32 day
// Markers are stored in canonical order, so the same set always encodes to the same bytes.

enum class DeathDecodeStatus {
    Ok,
    BadMagic,
    BadVersion,
    Truncated,
    TrailingBytes,
    BadLimits,     // zero caps, or maxTotal above the ceiling
    TooMany,       // the count exceeds maxTotal or cannot fit in the bytes given
    InvalidRecord, // bad coordinate or cause, out of canonical order, duplicate, or a cap exceeded
};

struct DeathDecodeResult {
    DeathDecodeStatus status = DeathDecodeStatus::Truncated;
    DeathMarkerSet set{0};
    bool ok() const { return status == DeathDecodeStatus::Ok; }
};

std::vector<std::uint8_t> encodeDeathMarkers(const DeathMarkerSet& set);

// Never throws or crashes on any input. On failure the returned set is empty.
DeathDecodeResult decodeDeathMarkers(std::span<const std::uint8_t> bytes);

} // namespace ichor
