#pragma once

#include "ichor/save/delta_store.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ichor {

namespace snapshot_limits {
inline constexpr std::size_t kMaxTailRecords = 1u << 16;
inline constexpr std::size_t kMaxImageBytes = 64u << 20;
}

inline constexpr std::uint8_t kSnapshotTailVersion = 1;

enum class TailLoadStatus : std::uint8_t {
    Clean,
    RecoveredTornTail,
    Unreadable,
};

struct TailLoadResult {
    TailLoadStatus status = TailLoadStatus::Unreadable;
    std::size_t recordsApplied = 0;
    std::size_t bytesDiscarded = 0;
};

// The in-memory append-only log of save state: one snapshot plus the deltas appended since.
// On disk it is a header and the snapshot frame, then one frame per tail record. Each frame
// carries its length and a checksum bound to its position, so a torn or damaged tail costs only
// the records from the damage onward and never yields a state that was not once the log's state.
class SnapshotTailLog {
public:
    // Nullopt when the snapshot breaks the delta limits.
    static std::optional<SnapshotTailLog> fromSnapshot(DeltaState snapshot);

    const DeltaState& snapshot() const { return snapshot_; }
    const DeltaState& state() const { return state_; }
    std::size_t tailLength() const { return tail_.size(); }

    // Appends the delta from the current state to `next`. Returns false and changes nothing when
    // `next` breaks the limits or the tail is full (compact, then append again). An unchanged
    // state appends nothing and returns true.
    bool append(const DeltaState& next);

    // Folds the tail into a new snapshot. Returns false and changes nothing when the state is too
    // large to encode as a snapshot.
    bool compact();

    // The full file image. Nullopt when the image would exceed kMaxImageBytes.
    std::optional<std::vector<std::uint8_t>> encode() const;

    // The bytes that extend an image holding the first `index` records by record `index`, for an
    // append-only write. Nullopt when index is out of range.
    std::optional<std::vector<std::uint8_t>> encodeTailFrame(std::size_t index) const;

    // Restores the snapshot and replays the tail. A damaged header or snapshot is Unreadable
    // (nullopt); a damaged tail keeps every record before the damage.
    static std::optional<SnapshotTailLog> load(const std::vector<std::uint8_t>& bytes, TailLoadResult* result = nullptr);

private:
    DeltaState snapshot_;
    DeltaState state_;
    std::vector<StateDelta> tail_;
    std::size_t imageBytes_ = 0;
};

// CRC-32 (IEEE, reflected), continuing from `crc` of previous bytes.
std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0);

}
