#include "ichor/core/session_summary.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <system_error>

namespace ichor {

namespace {

void appendString(std::string& out, std::string_view text)
{
    out += '"';
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(c));
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

std::string fpsField(double fps)
{
    return fps > 0.0 ? std::format("{:.1f}", fps) : "null";
}

bool looksLikePath(std::string_view value)
{
    return value.find_first_of("/\\:") != std::string_view::npos;
}

}

void FrameTimes::add(double seconds)
{
    if (std::isfinite(seconds) && seconds > 0.0) {
        seconds_.push_back(static_cast<float>(seconds));
    }
}

double FrameTimes::fpsAt(double percentile) const
{
    if (seconds_.empty()) {
        return 0.0;
    }
    std::vector<float> sorted = seconds_;
    const std::size_t rank = static_cast<std::size_t>(std::ceil(percentile / 100.0 * static_cast<double>(sorted.size())));
    const std::size_t index = std::clamp<std::size_t>(rank, 1, sorted.size()) - 1;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(index), sorted.end());
    return 1.0 / static_cast<double>(sorted[index]);
}

double FrameTimes::worstFps() const
{
    return seconds_.empty() ? 0.0 : 1.0 / static_cast<double>(*std::max_element(seconds_.begin(), seconds_.end()));
}

std::string sessionSummaryJson(const SessionSummary& summary)
{
    std::string out = "{\n  \"schema\": 1,\n  \"build_version\": ";
    appendString(out, summary.buildVersion);
    out += std::format(",\n  \"seed\": {},\n  \"flags\": [", summary.seed);
    for (std::size_t i = 0; i < summary.flags.size(); ++i) {
        if (i > 0) out += ", ";
        appendString(out, summary.flags[i]);
    }
    out += std::format("],\n  \"play_seconds\": {:.3f},\n  \"frames\": {},\n", summary.playSeconds, summary.frames.count());
    out += std::format("  \"fps\": {{\"p50\": {}, \"p95\": {}, \"p99\": {}, \"worst\": {}}},\n", fpsField(summary.frames.fpsAt(50.0)),
        fpsField(summary.frames.fpsAt(95.0)), fpsField(summary.frames.fpsAt(99.0)), fpsField(summary.frames.worstFps()));
    out += "  \"deaths_by_cause\": {";
    for (std::size_t i = 0; i < summary.deathsByCause.size(); ++i) {
        if (i > 0) out += ", ";
        appendString(out, summary.deathsByCause[i].first);
        out += std::format(": {}", summary.deathsByCause[i].second);
    }
    out += std::format("}},\n  \"retries\": {},\n  \"quit_time_utc\": ", summary.retries);
    appendString(out, utcTimestamp(summary.quitTime));
    out += "\n}\n";
    return out;
}

std::vector<std::string> scrubRunFlags(const std::vector<std::string>& args)
{
    std::vector<std::string> out;
    out.reserve(args.size());
    for (const std::string& arg : args) {
        out.push_back(looksLikePath(arg) ? "<path>" : arg);
    }
    return out;
}

std::string utcTimestamp(std::chrono::system_clock::time_point time)
{
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(time));
}

std::filesystem::path writeSessionFile(const std::filesystem::path& dir, std::string_view stem, std::chrono::system_clock::time_point time, std::string_view contents)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::is_directory(dir, ec)) {
        return {};
    }
    const std::string base = std::format("{}-{:%Y%m%d-%H%M%S}Z", stem, std::chrono::floor<std::chrono::seconds>(time));
    for (int copy = 1; copy <= 1000; ++copy) {
        const std::filesystem::path path = dir / (copy == 1 ? base + ".json" : std::format("{}-{}.json", base, copy));
        if (std::filesystem::exists(path, ec) || ec) {
            continue;
        }
        std::ofstream out(path, std::ios::binary);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.close();
        return out ? path : std::filesystem::path{};
    }
    return {};
}

}
