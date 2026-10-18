#include "ichor/ui/journal_screen_bind.h"

#include "ichor/ui/journal_screen.h"
#include "ichor/ui/ledger.h"

#include <format>

namespace ichor {

namespace {

constexpr std::uint8_t kMajorWeight = 128;

const char* tabId(JournalTab tab)
{
    return tab == JournalTab::Prose ? "tab-prose" : "tab-consequences";
}

}

std::string journalPlayTime(std::uint64_t tick, double secondsPerTick)
{
    const double elapsed = static_cast<double>(tick) * secondsPerTick;
    const std::uint64_t seconds = elapsed >= 0.0 && elapsed < 1.8e19 ? static_cast<std::uint64_t>(elapsed) : 0;
    return std::format("{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60);
}

void bindJournalModel(JournalScreen& screen, const std::vector<JournalModelEntry>& entries)
{
    screen.setTabs(buildJournalModel(entries));
}

std::string journalTabBarMarkup(const std::vector<JournalModelTab>& tabs, JournalTab active)
{
    std::string markup;
    for (const JournalModelTab& tab : tabs) {
        markup += std::format("<div id=\"{}\" class=\"tab{}\">{}</div>", tabId(tab.tab), tab.tab == active ? " active" : "", escapeRml(tab.title));
    }
    return markup;
}

std::string journalTabBodyMarkup(const std::vector<JournalModelTab>& tabs, JournalTab active)
{
    std::string markup;
    for (const JournalModelTab& tab : tabs) {
        if (tab.tab != active) {
            continue;
        }
        if (tab.groups.empty()) {
            return "<div class=\"empty\">Nothing has been written down yet.</div>";
        }
        for (const JournalModelGroup& group : tab.groups) {
            if (!group.title.empty()) {
                markup += "<div class=\"group\">" + escapeRml(group.title) + "</div>";
            }
            for (const JournalModelLine& line : group.lines) {
                markup += std::format("<div class=\"entry{}\"><span class=\"when\">{}</span>{}</div>", line.weight >= kMajorWeight ? " major" : "", escapeRml(line.time), escapeRml(line.text));
            }
            if (group.omitted > 0) {
                markup += std::format("<div class=\"more\">and {} more</div>", group.omitted);
            }
        }
        if (tab.omittedGroups > 0) {
            markup += std::format("<div class=\"more\">and {} more groups</div>", tab.omittedGroups);
        }
    }
    return markup;
}

}
