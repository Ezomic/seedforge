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
    bool operator==(const ChronicleEntry&) const = default;
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

    // False when the category is not plain or the chronicle is full. Tabs and
    // line breaks in the text become spaces, and long text is cut.
    bool add(std::uint64_t tick, std::string_view category, std::string_view text);

    const std::vector<ChronicleEntry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }
    void clear() { entries_.clear(); }

    // The saved form: one line per entry. parse() is all or nothing.
    std::string serialize() const;
    static bool parse(std::string_view bytes, Chronicle& out);

    // The newest `count` entries, newest first, for the journal screen.
    std::vector<ChronicleLine> recent(double secondsPerTick, std::size_t count) const;

    // Readable text for the journal and for sharing, with the time of each
    // entry as hours:minutes:seconds of simulated play.
    std::string exportText(double secondsPerTick) const;

    bool operator==(const Chronicle&) const = default;

private:
    std::vector<ChronicleEntry> entries_;
};

}
