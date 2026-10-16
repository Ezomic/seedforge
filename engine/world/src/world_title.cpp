#include "ichor/world/world_title.h"

#include <array>

namespace ichor {

namespace {

struct CataclysmPhrases {
    std::string_view id;
    std::array<std::string_view, 3> titles;
    std::string_view sentence;
};

struct SlotPhrases {
    std::string_view id;
    std::string_view title;
    std::string_view sentence;
};

// Each table is sorted by id, so the first entry present in the input is the smallest id.
constexpr std::array<CataclysmPhrases, 3> kCataclysms = {{
    {"cataclysm.artemis_withdrew", {"The Empty Hunt", "The Silent Wood", "The Unmothered Wild"}, "The mistress of wild things left, and the wild things went with her."},
    {"cataclysm.flood", {"The Salt Years", "The Drowned Age", "The Long Ebb"}, "The flood never fully drew back."},
    {"cataclysm.silence_of_the_oracles", {"The Years of Silence", "The Unanswered Age", "The Hushed Oracle"}, "The oracles stopped answering, and nobody can say whether the gods died, left or lost interest."},
}};

constexpr std::array<SlotPhrases, 3> kLaws = {{
    {"law.hecatomb", "The Price of Blood", "Power is bought with sacrifice."},
    {"law.last_favour", "The Last Favour", "Divine attention is nearly spent, and it will not return."},
    {"law.orichalcum", "The Ridge Age", "Power lives in a metal that forms along the volcanic ridges."},
}};

constexpr std::array<SlotPhrases, 3> kPowers = {{
    {"powers.hegemony", "The Long Afternoon", "The Hegemony still holds the roads, though its centre is failing."},
    {"powers.league", "The Contract Coast", "The League rules what is left of the coast, and everything is done by contract."},
    {"powers.satrapy", "The Foreign Yoke", "A foreign satrap came twenty years ago, and every town has picked a side."},
}};

constexpr std::string_view kUnwrittenTitle = "The Unwritten Age";
constexpr std::string_view kUnwrittenSummary = "A world waits to be named.";

bool contains(std::span<const std::string_view> ids, std::string_view id)
{
    for (const std::string_view candidate : ids) {
        if (candidate == id) {
            return true;
        }
    }
    return false;
}

template <typename Table>
const typename Table::value_type* pick(const Table& table, std::span<const std::string_view> ids)
{
    for (const auto& entry : table) {
        if (contains(ids, entry.id)) {
            return &entry;
        }
    }
    return nullptr;
}

std::uint64_t mix(std::uint64_t value)
{
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

void appendSentence(std::string& summary, std::string_view sentence)
{
    if (!summary.empty()) {
        summary += ' ';
    }
    summary += sentence;
}

}

WorldBlurb composeWorldBlurb(std::span<const std::string_view> axiomIds, std::uint64_t seed)
{
    const CataclysmPhrases* cataclysm = pick(kCataclysms, axiomIds);
    const SlotPhrases* law = pick(kLaws, axiomIds);
    const SlotPhrases* powers = pick(kPowers, axiomIds);

    WorldBlurb blurb;
    if (cataclysm != nullptr) {
        blurb.title = cataclysm->titles[mix(seed) % cataclysm->titles.size()];
    } else if (powers != nullptr) {
        blurb.title = powers->title;
    } else if (law != nullptr) {
        blurb.title = law->title;
    } else {
        blurb.title = kUnwrittenTitle;
    }

    if (cataclysm != nullptr) {
        appendSentence(blurb.summary, cataclysm->sentence);
    }
    if (powers != nullptr) {
        appendSentence(blurb.summary, powers->sentence);
    }
    if (law != nullptr) {
        appendSentence(blurb.summary, law->sentence);
    }
    if (blurb.summary.empty()) {
        blurb.summary = kUnwrittenSummary;
    }
    return blurb;
}

}
