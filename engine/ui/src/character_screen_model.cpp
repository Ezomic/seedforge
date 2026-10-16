#include "ichor/ui/character_screen_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string_view>
#include <utility>

namespace ichor {

namespace {

constexpr double kMaxStatValue = 1.0e9;
constexpr std::int32_t kStandingLimit = 1000;
constexpr std::int32_t kMaxGrowthPercent = 10000;

bool isContinuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

// Control characters become spaces, runs of spaces collapse, the ends are trimmed, and the cut
// never lands inside a UTF-8 sequence.
std::string clean(std::string_view text)
{
    std::string out;
    out.reserve(std::min(text.size(), kCharacterScreenMaxTextBytes + 4));
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (c < 0x20 || c == 0x7f || c == ' ') {
            if (!out.empty() && out.back() != ' ') {
                out += ' ';
            }
        } else {
            out += raw;
        }
        if (out.size() > kCharacterScreenMaxTextBytes + 4) {
            break;
        }
    }
    if (out.size() > kCharacterScreenMaxTextBytes) {
        std::size_t cut = kCharacterScreenMaxTextBytes;
        while (cut > 0 && isContinuation(static_cast<unsigned char>(out[cut]))) {
            --cut;
        }
        out.resize(cut);
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string labelOrId(std::string_view label, std::string_view id)
{
    std::string text = clean(label);
    return text.empty() ? clean(id) : text;
}

std::string_view statTitle(CharacterStat stat)
{
    switch (stat) {
    case CharacterStat::Health: return "Health";
    case CharacterStat::Stamina: return "Stamina";
    case CharacterStat::Carry: return "Carry";
    case CharacterStat::Attunement: return "Attunement";
    }
    return {};
}

bool validStat(double value)
{
    return std::isfinite(value) && value >= 0.0 && value <= kMaxStatValue;
}

void capLines(CharacterScreenSection& section, std::vector<CharacterScreenLine>& lines, std::size_t cap)
{
    section.omitted = lines.size() > cap ? lines.size() - cap : 0;
    lines.resize(std::min(lines.size(), cap));
    section.lines = std::move(lines);
}

std::vector<std::string> cleanedDetails(std::string_view prefix, const std::vector<std::string>& items)
{
    std::vector<std::string> shown;
    std::set<std::string> seen;
    for (const auto& item : items) {
        std::string text = clean(item);
        if (!text.empty() && seen.insert(text).second) {
            shown.push_back(std::move(text));
        }
    }
    std::sort(shown.begin(), shown.end());
    if (shown.empty()) {
        return {};
    }
    const std::size_t extra = shown.size() > kCharacterScreenMaxDetails ? shown.size() - kCharacterScreenMaxDetails : 0;
    shown.resize(shown.size() - extra);
    std::string joined(prefix);
    for (std::size_t i = 0; i < shown.size(); ++i) {
        joined += i == 0 ? "" : ", ";
        joined += shown[i];
    }
    if (extra > 0) {
        joined += " and " + std::to_string(extra) + " more";
    }
    return {std::move(joined)};
}

void addUnlockSection(std::vector<CharacterScreenSection>& view, const std::vector<CharacterScreenUnlock>& source,
    CharacterSectionKind kind, const char* title, std::size_t cap)
{
    struct Row {
        std::string id;
        std::string text;
        bool unlocked;
    };
    std::vector<Row> rows;
    std::set<std::string_view> seen;
    for (const auto& entry : source) {
        if (entry.id.empty() || !seen.insert(entry.id).second) {
            continue;
        }
        std::string text = labelOrId(entry.label, entry.id);
        if (!text.empty()) {
            rows.push_back({entry.id, std::move(text), entry.unlocked});
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.unlocked != b.unlocked ? a.unlocked : a.id < b.id;
    });
    std::vector<CharacterScreenLine> lines;
    for (auto& row : rows) {
        CharacterScreenLine line;
        line.text = std::move(row.text);
        line.unlocked = row.unlocked;
        lines.push_back(std::move(line));
    }
    if (!lines.empty()) {
        CharacterScreenSection section{kind, title, {}, 0};
        capLines(section, lines, cap);
        view.push_back(std::move(section));
    }
}

}

std::vector<CharacterScreenSection> buildCharacterScreen(const CharacterScreenInput& input)
{
    std::vector<CharacterScreenSection> view;

    {
        std::array<const CharacterScreenStat*, 4> byStat{};
        for (const auto& entry : input.stats) {
            const auto index = static_cast<std::size_t>(entry.stat);
            if (index < byStat.size() && byStat[index] == nullptr && validStat(entry.current)) {
                byStat[index] = &entry;
            }
        }
        std::vector<CharacterScreenLine> lines;
        for (const auto* entry : byStat) {
            if (entry == nullptr) {
                continue;
            }
            CharacterScreenLine line;
            line.text = std::string(statTitle(entry->stat));
            line.value = static_cast<std::int32_t>(std::lround(entry->current));
            if (validStat(entry->base) && entry->base > 0.0) {
                const double growth = std::max(0.0, (entry->current / entry->base - 1.0) * 100.0);
                line.growthPercent = static_cast<std::int32_t>(std::lround(std::min(growth, double(kMaxGrowthPercent))));
            }
            lines.push_back(std::move(line));
        }
        if (!lines.empty()) {
            view.push_back({CharacterSectionKind::Stats, "Stats", std::move(lines), 0});
        }
    }

    addUnlockSection(view, input.capabilities, CharacterSectionKind::Capabilities, "Capabilities",
        kCharacterScreenMaxCapabilityLines);
    addUnlockSection(view, input.verbs, CharacterSectionKind::Verbs, "Traversal", kCharacterScreenMaxVerbLines);

    {
        struct Row {
            std::string faction;
            const CharacterScreenStanding* source;
        };
        std::vector<Row> rows;
        std::set<std::string> seen;
        for (const auto& entry : input.standing) {
            std::string name = clean(entry.faction);
            if (!name.empty() && seen.insert(name).second) {
                rows.push_back({std::move(name), &entry});
            }
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.faction < b.faction; });
        std::vector<CharacterScreenLine> lines;
        for (auto& row : rows) {
            const auto& s = *row.source;
            CharacterScreenLine line;
            line.text = std::move(row.faction);
            line.value = std::clamp(s.standing, -kStandingLimit, kStandingLimit);
            if (const std::string tier = clean(s.tier); !tier.empty()) {
                line.details.push_back("Tier: " + tier);
            }
            for (auto& d : cleanedDetails("Opens: ", s.unlocksNow)) {
                line.details.push_back(std::move(d));
            }
            if (const std::string next = clean(s.nextTier); !next.empty()) {
                std::string text = "Next: " + next;
                if (s.pointsToNext > 0) {
                    text += " in " + std::to_string(s.pointsToNext);
                }
                line.details.push_back(std::move(text));
                for (auto& d : cleanedDetails("Will open: ", s.unlocksNext)) {
                    line.details.push_back(std::move(d));
                }
            }
            lines.push_back(std::move(line));
        }
        if (!lines.empty()) {
            CharacterScreenSection section{CharacterSectionKind::Standing, "Standing", {}, 0};
            capLines(section, lines, kCharacterScreenMaxStandingLines);
            view.push_back(std::move(section));
        }
    }

    return view;
}

}
