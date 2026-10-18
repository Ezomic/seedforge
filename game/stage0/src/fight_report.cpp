#include "ichor/stage0/fight_report.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace ichor::stage0 {

namespace {

constexpr std::size_t kColumns = 23;

std::vector<std::string_view> split(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (fields.size() <= kColumns) {
        const std::size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    return fields;
}

bool number(std::string_view text, double& out)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && std::isfinite(out);
}

bool count(std::string_view text, int& out)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && out >= 0;
}

bool causeFromName(std::string_view text, DeathCause& out)
{
    for (std::size_t i = 0; i < kDeathCauseCount; ++i) {
        const DeathCause cause = static_cast<DeathCause>(i);
        if (text == name(cause)) {
            out = cause;
            return true;
        }
    }
    return false;
}

bool parseRow(std::string_view line, FightRow& row)
{
    const std::vector<std::string_view> f = split(line);
    // Columns are only ever appended, so a newer log with extra trailing fields still parses.
    if (f.size() < kColumns) {
        return false;
    }
    if (f[3] != "player" && f[3] != "foe") {
        return false;
    }
    row.playerLost = f[3] == "player";
    return count(f[0], row.fight) && number(f[1], row.seconds) && number(f[2], row.gapToRetry) && causeFromName(f[4], row.cause)
        && number(f[5], row.playerHealthLeft) && count(f[6], row.playerAttacks) && count(f[7], row.playerHits)
        && count(f[8], row.playerWhiffs) && count(f[9], row.playerBlocked) && count(f[10], row.playerDodges)
        && number(f[13], row.playerDamageTaken);
}

double median(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    return values.size() % 2 == 1 ? values[mid] : (values[mid - 1] + values[mid]) * 0.5;
}

}

std::vector<FightRow> parseFightLog(std::string_view csv)
{
    std::vector<FightRow> rows;
    bool first = true;
    while (!csv.empty()) {
        const std::size_t end = csv.find('\n');
        std::string_view line = csv.substr(0, end);
        csv = end == std::string_view::npos ? std::string_view{} : csv.substr(end + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const bool header = first && line.starts_with("fight,");
        first = false;
        FightRow row;
        if (!header && parseRow(line, row)) {
            rows.push_back(row);
        }
    }
    return rows;
}

FightSummary summarise(const std::vector<FightRow>& rows)
{
    FightSummary summary;
    std::vector<double> seconds;
    std::vector<double> gaps;
    for (const FightRow& row : rows) {
        summary.fights += 1;
        seconds.push_back(row.seconds);
        if (row.gapToRetry > 0.0) {
            gaps.push_back(row.gapToRetry);
        }
        if (row.playerLost) {
            summary.playerLosses += 1;
            summary.playerDeaths.add(row.cause);
        } else {
            summary.playerWins += 1;
        }
    }
    summary.medianSeconds = median(std::move(seconds));
    summary.medianGapToRetry = median(std::move(gaps));
    summary.diagnosis = diagnose(summary.playerDeaths);
    return summary;
}

std::string formatReport(const FightSummary& summary)
{
    std::string text = std::format("fights {} (won {}, lost {}), median {:.1f}s, median retry gap {:.1f}s\n", summary.fights,
        summary.playerWins, summary.playerLosses, summary.medianSeconds, summary.medianGapToRetry);
    for (std::size_t i = 0; i < kDeathCauseCount; ++i) {
        const DeathCause cause = static_cast<DeathCause>(i);
        if (summary.playerDeaths.count(cause) > 0) {
            text += std::format("  {:<15} {:>3}  {:>5.1f}%\n", name(cause), summary.playerDeaths.count(cause), summary.playerDeaths.share(cause) * 100.0);
        }
    }
    text += std::format("diagnosis: {}\n", name(summary.diagnosis));
    return text;
}

}
