#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The story of a campaign: notable events only, human-readable, small (doc 14).
// The state ledger holds current truth; the chronicle holds what happened.
struct ChronicleEntry {
    std::uint64_t tick = 0;
    std::string category; // lowercase letters, digits, dash, underscore
    std::string text;
    // Who it happened to or near, where, and how much it mattered (0 to 255).
    // An untagged entry has no NPCs, place 0 and weight 0.
    std::vector<std::uint32_t> npcs; // ascending, no duplicates, at most kMaxTaggedNpcs
    std::uint32_t place = 0;
    std::uint8_t weight = 0;
    bool operator==(const ChronicleEntry&) const = default;
};

struct ChronicleTags {
    std::vector<std::uint32_t> npcs;
    std::uint32_t place = 0;
    std::uint8_t weight = 0;
};

// One entry as the journal shows it: the time of play as hours:minutes:seconds.
struct ChronicleLine {
    std::string time;
    std::string category;
    std::string text;
};

class Chronicle {
public:
    static constexpr std::size_t kMaxEntries = 100000;
    static constexpr std::size_t kMaxTextBytes = 500;
    static constexpr std::size_t kMaxTaggedNpcs = 8;

    // False when the category is not plain or the chronicle is full. Tabs and
    // line breaks in the text become spaces, and long text is cut.
    bool add(std::uint64_t tick, std::string_view category, std::string_view text);

    // Same, with tags for NPC memory. The NPC ids are sorted and deduplicated;
    // false when more than kMaxTaggedNpcs remain.
    bool add(std::uint64_t tick, std::string_view category, std::string_view text, const ChronicleTags& tags);

    const std::vector<ChronicleEntry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }
    void clear() { entries_.clear(); }

    // The saved form: one line per entry. parse() is all or nothing. A tagged
    // entry adds a fourth tab-separated column "npc,npc|place|weight"; an
    // untagged entry is written exactly as before.
    std::string serialize() const;
    static bool parse(std::string_view bytes, Chronicle& out);

    // The newest `count` entries, newest first, for the journal screen.
    std::vector<ChronicleLine> recent(double secondsPerTick, std::size_t count) const;

    // The same, for the entries of one category only.
    std::vector<ChronicleLine> recentIn(std::string_view category, double secondsPerTick, std::size_t count) const;

    // Readable text for the journal and for sharing, with the time of each
    // entry as hours:minutes:seconds of simulated play.
    std::string exportText(double secondsPerTick) const;

    bool operator==(const Chronicle&) const = default;

private:
    std::vector<ChronicleEntry> entries_;
};

}
