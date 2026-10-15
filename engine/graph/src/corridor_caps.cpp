#include "ichor/graph/corridor_caps.h"

#include "ichor/core/rng.h"

#include <cmath>
#include <utility>

namespace ichor::graph::corridor {

namespace {

bool finiteAtLeast(float value, float minimum)
{
    return std::isfinite(value) && value >= minimum;
}

Result failure(std::string message)
{
    Result r;
    r.error = std::move(message);
    return r;
}

Segment forcedVariation(Rng& rng, float width)
{
    Segment s;
    s.width = width;
    switch (rng.nextU32() % 3u) {
    case 0:
        s.variation = Variation::Bend;
        break;
    case 1: {
        s.variation = Variation::WidthChange;
        const float narrower = width * 0.75f;
        const float wider = width * 1.25f;
        const bool preferNarrow = (rng.nextU32() & 1u) != 0u;
        const bool useNarrower = preferNarrow ? narrower >= kMinWidth : wider > kMaxWidth;
        s.width = useNarrower ? narrower : wider;
        break;
    }
    default:
        s.variation = Variation::Landmark;
        break;
    }
    return s;
}

} // namespace

Result applyCaps(const std::vector<Segment>& path, const Params& params)
{
    if (!finiteAtLeast(params.maxStraight, kMinCap))
        return failure("maxStraight must be finite and at least kMinCap");
    if (path.size() > kMaxInputSegments)
        return failure("path has too many segments");

    for (const Segment& s : path) {
        if (!finiteAtLeast(s.length, 0.0f))
            return failure("segment length must be finite and not negative");
        if (!std::isfinite(s.width) || s.width < kMinWidth || s.width > kMaxWidth)
            return failure("segment width out of range");
        if (static_cast<std::uint8_t>(s.variation) > static_cast<std::uint8_t>(Variation::Landmark))
            return failure("unknown variation");
    }

    Rng rng(params.seed);
    Result result;
    float run = 0.0f;

    const auto emit = [&result](const Segment& s) {
        if (result.segments.size() >= kMaxOutputSegments)
            return false;
        result.segments.push_back(s);
        return true;
    };
    const char* tooMany = "corridor needs too many segments";

    for (const Segment& s : path) {
        if (s.variation != Variation::None) {
            if (!emit(s))
                return failure(tooMany);
            run = 0.0f;
            continue;
        }

        float width = s.width;
        float remaining = s.length;
        while (remaining > 0.0f) {
            const float room = params.maxStraight - run;
            if (remaining <= room) {
                if (!emit(Segment{remaining, width, Variation::None}))
                    return failure(tooMany);
                run += remaining;
                break;
            }
            if (!emit(Segment{room, width, Variation::None}))
                return failure(tooMany);
            remaining -= room;
            const Segment forced = forcedVariation(rng, width);
            if (!emit(forced))
                return failure(tooMany);
            width = forced.width;
            run = 0.0f;
        }
    }
    return result;
}

float longestStraightRun(const std::vector<Segment>& path)
{
    float longest = 0.0f;
    float run = 0.0f;
    for (const Segment& s : path) {
        if (s.variation != Variation::None) {
            run = 0.0f;
            continue;
        }
        run += s.length;
        if (run > longest)
            longest = run;
    }
    return longest;
}

} // namespace ichor::graph::corridor
