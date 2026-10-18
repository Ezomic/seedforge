#include "ichor/stage0/session_ledger.h"

#include <charconv>
#include <format>

namespace ichor::stage0 {

std::string cleanField(std::string_view text)
{
    std::string out;
    for (const char c : text) {
        if (out.size() >= kMaxLabelLength) {
            break;
        }
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f || c == ',' || c == '"') {
            continue;
        }
        out.push_back(c);
    }
    return out;
}

const char* ledgerHeader()
{
    return "fight,session,variable,bug_fix\n";
}

std::string ledgerRow(int fight, const SessionMarker& marker)
{
    return std::format("{},{},{},{}\n", fight, cleanField(marker.label), cleanField(marker.variable), isBugFixFight(fight) ? 1 : 0);
}

GateProgress readProgress(std::string_view text)
{
    GateProgress progress;
    std::set<int> seen;
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
        const std::size_t first = line.find(',');
        if (first == std::string_view::npos) {
            continue;
        }
        int fight = 0;
        const std::string_view number = line.substr(0, first);
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), fight);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || fight < 1) {
            continue;
        }
        if (!seen.insert(fight).second) {
            continue;
        }
        const std::size_t second = line.find(',', first + 1);
        const std::string_view session = line.substr(first + 1, second == std::string_view::npos ? std::string_view::npos : second - first - 1);
        if (!session.empty()) {
            progress.sessions.insert(cleanField(session));
        }
        progress.recorded += 1;
        if (isBugFixFight(fight)) {
            progress.bugFix += 1;
        }
    }
    progress.tuned = progress.recorded - progress.bugFix;
    progress.remaining = progress.recorded >= kGateFights ? 0 : kGateFights - progress.recorded;
    progress.complete = progress.recorded >= kGateFights;
    return progress;
}

std::string progressLine(const GateProgress& p)
{
    return std::format("{}/{} fights ({} bug-fix window, {} tuning), {} to go, {} sessions{}",
        p.recorded, kGateFights, p.bugFix, p.tuned, p.remaining, p.sessions.size(), p.complete ? ", COMPLETE" : "");
}

}
