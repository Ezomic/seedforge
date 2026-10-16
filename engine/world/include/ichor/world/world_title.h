#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ichor {

// The new-game block of doc 01: a title and a short summary paragraph.
struct WorldBlurb {
    std::string title;
    std::string summary;

    bool operator==(const WorldBlurb&) const = default;
};

// Composes the blurb from chosen axiom ids such as "cataclysm.flood". Only the slots shown at
// creation (Cataclysm, Law, Powers) speak; Pressure and Anomaly ids are ignored so the prophecy and
// the anomaly stay revelations. Unknown or empty ids are ignored too, a missing slot just leaves out
// its sentence, and when several ids share a slot the smallest known id wins, so the result does not
// depend on the order given. `seed` only picks between equally good phrasings. Pure and
// deterministic, ASCII output without dashes.
WorldBlurb composeWorldBlurb(std::span<const std::string_view> axiomIds, std::uint64_t seed);

}
