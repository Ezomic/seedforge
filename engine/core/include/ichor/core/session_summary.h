#pragma once

#include "ichor/core/version.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ichor {

// Every frame's wall time, for the fps percentiles a playtest summary reports.
class FrameTimes {
public:
    // A frame of no time or a non-finite one is not counted, so it cannot make an infinite fps.
    void add(double seconds);
    std::uint64_t count() const { return seconds_.size(); }
    // The fps of the frame at this percentile counted from the fastest (nearest rank), so p99 is the
    // speed 99 in 100 frames reached. 0 with no frames.
    double fpsAt(double percentile) const;
    double worstFps() const;

private:
    std::vector<float> seconds_;
};

// The JSON a playtester sends back by hand after a session (SEED-696). Nothing here is uploaded, and
// nothing names the person or their machine: flags go through scrubRunFlags first.
struct SessionSummary {
    std::string buildVersion = std::string(versionStamp());
    std::uint64_t seed = 0;
    std::vector<std::string> flags;
    double playSeconds = 0.0;
    FrameTimes frames;
    std::vector<std::pair<std::string, int>> deathsByCause;
    int retries = 0;
    std::chrono::system_clock::time_point quitTime;
};

std::string sessionSummaryJson(const SessionSummary& summary);

// The command line with every value that looks like a path (a slash, a backslash or a drive colon)
// replaced by "<path>", so a summary never carries a user name from a folder.
std::vector<std::string> scrubRunFlags(const std::vector<std::string>& args);

// "2026-10-28T15:04:05Z"
std::string utcTimestamp(std::chrono::system_clock::time_point time);

// Writes contents to dir/<stem>-<YYYYMMDD-HHMMSS>Z.json, creating dir, and adds -2, -3 and so on rather
// than replace a file already there. The path written, or empty when it could not be.
std::filesystem::path writeSessionFile(const std::filesystem::path& dir, std::string_view stem, std::chrono::system_clock::time_point time, std::string_view contents);

// Creates the folder dir/<stem>-<YYYYMMDD-HHMMSS>Z, with -2, -3 and so on rather than reuse one already there.
// The folder made, or empty when it could not be.
std::filesystem::path createSessionFolder(const std::filesystem::path& dir, std::string_view stem, std::chrono::system_clock::time_point time);

}
