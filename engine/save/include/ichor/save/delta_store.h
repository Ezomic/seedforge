#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

// A flat key-value view of save state. Keys are stable, structure-derived ids (doc 14), so the
// same key names the same thing in the baseline and in the current state. std::map keeps
// iteration order deterministic.
using DeltaState = std::map<std::string, std::vector<std::uint8_t>>;

// What differs between a baseline and a current state. A key whose value equals the baseline
// is elided; a key present only in the baseline is recorded as removed.
struct StateDelta {
    DeltaState set;
    std::vector<std::string> removed;

    bool empty() const { return set.empty() && removed.empty(); }
    bool operator==(const StateDelta&) const = default;
};

namespace delta_limits {
inline constexpr std::size_t kMaxKeyBytes = 256;
inline constexpr std::size_t kMaxValueBytes = 1u << 20;
inline constexpr std::size_t kMaxEntries = 1u << 16;
inline constexpr std::size_t kMaxEncodedBytes = 16u << 20;
}

inline constexpr std::uint8_t kDeltaFormatVersion = 1;

// The delta that takes `baseline` to `current`.
StateDelta computeDelta(const DeltaState& baseline, const DeltaState& current);

// The state a delta describes on top of `baseline`. Nullopt when the delta does not fit the
// baseline (it removes a key the baseline lacks) or breaks the key and size limits.
std::optional<DeltaState> applyDelta(const DeltaState& baseline, const StateDelta& delta);

// Deterministic bytes: version byte, then entries sorted by key. Nullopt when the delta breaks
// the key and size limits, has an empty or duplicate key, or lists a key as both set and removed.
std::optional<std::vector<std::uint8_t>> encodeDelta(const StateDelta& delta);

// Nullopt for an unknown version, truncation, trailing bytes, unsorted or duplicate keys, a key in
// both lists, or any size beyond the limits. Never allocates beyond what the bytes can hold.
std::optional<StateDelta> decodeDelta(const std::vector<std::uint8_t>& bytes);

}
