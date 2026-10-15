#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor::graph::corridor {

enum class Variation : std::uint8_t {
    None,        // a plain straight piece
    Bend,        // the path turns
    WidthChange, // the corridor narrows or widens
    Landmark,    // something to look at breaks the sightline
};

// One piece of a corridor path. A piece with a variation other than None is a
// zero-length marker between straight pieces and resets the straight run.
struct Segment {
    float length = 0.0f;
    float width = 2.0f;
    Variation variation = Variation::None;
};

inline constexpr float kMinCap = 1.0f;
inline constexpr float kMinWidth = 0.5f;
inline constexpr float kMaxWidth = 16.0f;
inline constexpr std::size_t kMaxInputSegments = 1024;
inline constexpr std::size_t kMaxOutputSegments = 4096;

struct Params {
    std::uint64_t seed = 0;
    float maxStraight = 12.0f; // longest straight run allowed, at least kMinCap
};

struct Result {
    std::vector<Segment> segments;
    std::string error; // empty on success; segments is empty otherwise
};

// Splits straight runs so none is longer than maxStraight, inserting a forced
// variation (chosen by the seed) each time a cap is hit with more straight
// path still to come. A run of exactly maxStraight needs no variation. Zero
// length straight pieces are dropped. Pure and deterministic: the same path
// and params give the same output. NaN, infinite, negative or oversized input,
// and a result that would exceed kMaxOutputSegments, yield an error instead
// of a path.
Result applyCaps(const std::vector<Segment>& path, const Params& params);

// Length of the longest straight run in a path (bends, width changes and
// landmarks end a run).
float longestStraightRun(const std::vector<Segment>& path);

} // namespace ichor::graph::corridor
