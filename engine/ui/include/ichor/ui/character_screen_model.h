#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

// The four stats of the spine (docs/07-progression.md); there are no others.
enum class CharacterStat : std::uint8_t { Health, Stamina, Carry, Attunement };

struct CharacterScreenStat {
    CharacterStat stat = CharacterStat::Health;
    double current = 0.0;
    // The value at the start of the campaign, used to show growth.
    double base = 0.0;
};

struct CharacterScreenUnlock {
    std::string id;
    std::string label;
    bool unlocked = false;
};

struct CharacterScreenStanding {
    std::string faction;
    // Clamped to [-1000, 1000], the range of standing.h.
    std::int32_t standing = 0;
    std::string tier;
    // What the current tier opens, and what the next one will; ids already turned into text.
    std::vector<std::string> unlocksNow;
    std::string nextTier;
    std::vector<std::string> unlocksNext;
    std::int32_t pointsToNext = 0;
};

// One place the player found, with the XP its first entry paid.
struct CharacterScreenDiscovery {
    std::string place;
    std::int64_t xp = 0;
};

struct CharacterScreenInput {
    std::vector<CharacterScreenStat> stats;
    std::vector<CharacterScreenUnlock> capabilities;
    std::vector<CharacterScreenUnlock> verbs;
    std::vector<CharacterScreenStanding> standing;
    // In the order found; the total line sums the entries, so it is never shown without at least one.
    std::vector<CharacterScreenDiscovery> discoveries;
};

enum class CharacterSectionKind : std::uint8_t { Stats, Capabilities, Verbs, Standing, Discoveries };

struct CharacterScreenLine {
    std::string text;
    // Stats: the whole-number value. Standing: the signed standing.
    std::optional<std::int32_t> value;
    // Stats: growth over base in whole percent, never negative.
    std::optional<std::int32_t> growthPercent;
    // Capabilities and verbs: whether the player has it.
    bool unlocked = false;
    // Standing: the tier, what it opens, and what the next tier buys.
    std::vector<std::string> details;

    bool operator==(const CharacterScreenLine&) const = default;
};

struct CharacterScreenSection {
    CharacterSectionKind kind = CharacterSectionKind::Stats;
    std::string title;
    std::vector<CharacterScreenLine> lines;
    // Entries dropped by the line cap.
    std::size_t omitted = 0;

    bool operator==(const CharacterScreenSection&) const = default;
};

constexpr std::size_t kCharacterScreenMaxCapabilityLines = 40;
constexpr std::size_t kCharacterScreenMaxVerbLines = 16;
constexpr std::size_t kCharacterScreenMaxStandingLines = 12;
constexpr std::size_t kCharacterScreenMaxDiscoveryLines = 12;
constexpr std::size_t kCharacterScreenMaxDetails = 8;
constexpr std::size_t kCharacterScreenMaxTextBytes = 80;

// Sections in fixed order (Stats, Capabilities, Verbs, Standing, Discoveries); a section with nothing to show is
// left out. Stats follow spine order and a repeated stat keeps its first valid entry; a stat with a
// non-finite, negative or absurdly large value is dropped, and growth is omitted when base is not
// positive. Capabilities and verbs list unlocked entries first, then by id, repeated ids once.
// Standing sorts by faction name, repeated factions keep the first entry, a faction with no name is
// dropped, and a next tier shows only when named. Discoveries lead with the total XP, then the most recent finds
// (newest first, a negative or unnamed entry dropped). Text is stripped of
// control characters and each text or detail item is cut to kCharacterScreenMaxTextBytes on a UTF-8
// boundary (a joined detail line is bounded by kCharacterScreenMaxDetails items), and an entry with
// no usable label falls back to its id. Pure and deterministic.
std::vector<CharacterScreenSection> buildCharacterScreen(const CharacterScreenInput& input);

}
