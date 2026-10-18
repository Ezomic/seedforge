#include "ichor/stage0/session_dashboard.h"

#include "ichor/stage0/session_ledger.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <map>
#include <set>

namespace ichor::stage0 {

namespace {

std::map<int, SessionMarker> readMarkers(std::string_view text)
{
    std::map<int, SessionMarker> markers;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const std::size_t c1 = line.find(',');
        if (c1 == std::string_view::npos) {
            continue;
        }
        int fight = 0;
        const std::string_view number = line.substr(0, c1);
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), fight);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || fight < 1 || markers.contains(fight)) {
            continue;
        }
        const std::size_t c2 = line.find(',', c1 + 1);
        const std::size_t c3 = c2 == std::string_view::npos ? c2 : line.find(',', c2 + 1);
        SessionMarker marker;
        marker.label = cleanField(line.substr(c1 + 1, c2 == std::string_view::npos ? std::string_view::npos : c2 - c1 - 1));
        if (c2 != std::string_view::npos) {
            marker.variable = cleanField(line.substr(c2 + 1, c3 == std::string_view::npos ? std::string_view::npos : c3 - c2 - 1));
        }
        markers.emplace(fight, std::move(marker));
    }
    return markers;
}

// Pipes would split a markdown cell.
std::string cell(std::string text)
{
    std::replace(text.begin(), text.end(), '|', '/');
    return text.empty() ? "-" : text;
}

std::string deathsCell(const DeathTally& deaths)
{
    std::string text;
    for (std::size_t i = 0; i < kDeathCauseCount; ++i) {
        const DeathCause cause = static_cast<DeathCause>(i);
        if (deaths.count(cause) > 0) {
            text += std::format("{}{} {}", text.empty() ? "" : ", ", name(cause), deaths.count(cause));
        }
    }
    return text.empty() ? "none" : text;
}

}

Dashboard buildDashboard(const std::vector<FightRow>& fights, std::string_view ledgerText)
{
    const std::map<int, SessionMarker> markers = readMarkers(ledgerText);
    Dashboard dashboard;
    std::map<std::string, std::size_t> index;
    std::vector<double> lostSeconds;
    std::set<int> seen;
    for (const FightRow& fight : fights) {
        if (!seen.insert(fight.fight).second) {
            continue;
        }
        if (isBugFixFight(fight.fight)) {
            dashboard.bugFixFightsSkipped += 1;
            continue;
        }
        const auto found = markers.find(fight.fight);
        const bool labelled = found != markers.end() && !found->second.label.empty();
        const std::string label = labelled ? found->second.label : kUnlabelledSession;
        const auto [it, added] = index.try_emplace(label, dashboard.sessions.size());
        if (added) {
            SessionRow row;
            row.label = label;
            row.firstFight = fight.fight;
            dashboard.sessions.push_back(row);
            lostSeconds.push_back(0.0);
        }
        SessionRow& row = dashboard.sessions[it->second];
        row.firstFight = std::min(row.firstFight, fight.fight);
        if (row.variable.empty() && found != markers.end()) {
            row.variable = found->second.variable;
        }
        row.fights += 1;
        if (fight.playerLost) {
            row.playerLosses += 1;
            row.deaths.add(fight.cause);
            lostSeconds[it->second] += fight.seconds;
        }
    }
    for (std::size_t i = 0; i < dashboard.sessions.size(); ++i) {
        SessionRow& row = dashboard.sessions[i];
        row.meanSecondsToKill = row.playerLosses > 0 ? lostSeconds[i] / row.playerLosses : 0.0;
        row.diagnosis = diagnose(row.deaths);
    }
    std::stable_sort(dashboard.sessions.begin(), dashboard.sessions.end(),
        [](const SessionRow& a, const SessionRow& b) { return a.firstFight < b.firstFight; });
    return dashboard;
}

std::string formatDashboard(const Dashboard& dashboard)
{
    std::string text = "| # | Variable changed | Fights | Deaths by cause | Mean time to kill | Diagnosis |\n|---|---|---|---|---|---|\n";
    if (dashboard.sessions.empty()) {
        text += "| - | *(no tuning fights)* | 0 | none | - | - |\n";
    }
    for (const SessionRow& row : dashboard.sessions) {
        const std::string ttk = row.playerLosses > 0 ? std::format("{:.1f} s", row.meanSecondsToKill) : "-";
        text += std::format("| {} | {} | {} | {} | {} | {} |\n", cell(row.label), cell(row.variable), row.fights, deathsCell(row.deaths), ttk,
            name(row.diagnosis));
    }
    if (dashboard.bugFixFightsSkipped > 0) {
        text += std::format("\n{} bug-fix window fights (1 to {}) are not counted.\n", dashboard.bugFixFightsSkipped, kBugFixFights);
    }
    return text;
}

}
