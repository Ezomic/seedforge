#include "ichor/ui/journal_screen_model.h"

#include "ichor/data/journal.h"

#include <algorithm>
#include <map>
#include <string_view>

namespace ichor {

namespace {

bool isContinuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

std::string clean(std::string_view text, std::size_t limit)
{
    std::string out;
    out.reserve(std::min(text.size(), limit + 4));
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (c < 0x20 || c == 0x7f || c == ' ') {
            if (!out.empty() && out.back() != ' ') {
                out += ' ';
            }
        } else {
            out += raw;
        }
        if (out.size() > limit + 4) {
            break;
        }
    }
    if (out.size() > limit) {
        std::size_t cut = limit;
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

std::string groupTitle(std::string_view category)
{
    std::string spaced(category);
    std::replace_if(spaced.begin(), spaced.end(), [](char c) { return c == '-' || c == '_'; }, ' ');
    std::string title = clean(spaced, kJournalModelMaxTitleBytes);
    if (title.empty()) {
        return "Other";
    }
    if (title[0] >= 'a' && title[0] <= 'z') {
        title[0] = static_cast<char>(title[0] - 'a' + 'A');
    }
    return title;
}

struct Item {
    std::size_t index = 0;
    std::uint64_t tick = 0;
    JournalModelLine line;
};

// Newest first; equal ticks keep the later input first.
bool newer(const Item& a, const Item& b)
{
    return a.tick != b.tick ? a.tick > b.tick : a.index > b.index;
}

JournalModelGroup makeGroup(std::string title, std::vector<Item>& items, std::size_t cap)
{
    std::sort(items.begin(), items.end(), newer);
    JournalModelGroup group;
    group.title = std::move(title);
    for (Item& item : items) {
        if (group.lines.size() == cap) {
            ++group.omitted;
        } else {
            group.lines.push_back(std::move(item.line));
        }
    }
    return group;
}

}

std::vector<JournalModelTab> buildJournalModel(const std::vector<JournalModelEntry>& entries)
{
    std::vector<Item> prose;
    std::map<std::string, std::vector<Item>> byTitle;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const JournalModelEntry& e = entries[i];
        Item item{i, e.tick, {clean(e.time, kJournalModelMaxTimeBytes), clean(e.text, kJournalModelMaxTextBytes), e.weight}};
        if (item.line.text.empty()) {
            continue;
        }
        if (e.category == kJournalCategory) {
            prose.push_back(std::move(item));
        } else {
            byTitle[groupTitle(e.category)].push_back(std::move(item));
        }
    }

    std::vector<JournalModelTab> view(2);
    view[0].tab = JournalTab::Prose;
    view[0].title = "Journal";
    view[1].tab = JournalTab::Consequences;
    view[1].title = "What I've done";

    if (!prose.empty()) {
        view[0].groups.push_back(makeGroup({}, prose, kJournalModelMaxProseLines));
    }

    std::vector<JournalModelGroup> groups;
    std::vector<std::uint64_t> newest;
    std::vector<std::size_t> order;
    for (auto& [title, items] : byTitle) {
        const auto top = std::min_element(items.begin(), items.end(), newer);
        newest.push_back(top->tick);
        order.push_back(groups.size());
        groups.push_back(makeGroup(title, items, kJournalModelMaxGroupLines));
    }
    // byTitle iterates by title, so a stable sort on newest tick leaves ties in title order.
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return newest[a] > newest[b]; });
    for (const std::size_t i : order) {
        if (view[1].groups.size() == kJournalModelMaxGroups) {
            ++view[1].omittedGroups;
        } else {
            view[1].groups.push_back(std::move(groups[i]));
        }
    }
    return view;
}

}
